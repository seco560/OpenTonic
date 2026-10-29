// The whole exe, decompiled and indexed (docs/EXE.md "Full dump"): one C file per function, with the types applied to
// the project (ApplyTypes.java), and index.json with what each function calls, which globals it reads and writes, which
// struct fields it reads and writes, which functions it takes the address of (dispatch tables, callbacks) and which
// strings it uses. Read-only: nothing in the project changes. tools/exe/xref.py queries the result.
// Usage: analyzeHeadless files/out/_decomp_artifacts/ghidra TonicTrouble -process -noanalysis -readOnly \
//          -scriptPath tools/ghidra -postScript DumpAll.java files/out/_decomp_artifacts/all
//@category RETonicTrouble
import ghidra.app.decompiler.*;
import ghidra.app.decompiler.parallel.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import ghidra.util.task.TaskMonitor;

import java.io.FileWriter;
import java.nio.file.*;
import java.util.*;

public class DumpAll extends GhidraScript {
    /** What the decompiler gives for one function (made on a worker thread). */
    static class Decompiled {
        Function f;
        String c, signature, error;
        Set<String> fieldsRead = new TreeSet<>(), fieldsWritten = new TreeSet<>();
    }

    static String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.toString();
    }

    static String list(Collection<String> xs) {
        StringBuilder b = new StringBuilder("[");
        for (String x : xs) b.append(b.length() > 1 ? "," : "").append('"').append(esc(x)).append('"');
        return b.append(']').toString();
    }

    /** "Struct.field@0x18" for a field token: the composite it belongs to, its name, its offset. */
    static String field(ClangFieldToken t) {
        DataType dt = t.getDataType();
        return (dt != null ? dt.getName() : "?") + "." + t.getText() + "@0x" + Integer.toHexString(t.getOffset());
    }

    /** Field reads and writes of one statement. In an assignment, the field written is the last one before the '=' outside
     *  brackets (`a->b->c = x` writes c and reads b; `p->arr[q->i] = x` writes arr and reads i); every other is read. */
    static void statement(ClangStatement st, Decompiled d) {
        List<ClangNode> toks = new ArrayList<>();
        st.flatten(toks);
        int assign = -1;
        for (int i = 0; i < toks.size(); i++)
            if (toks.get(i) instanceof ClangOpToken && "=".equals(toks.get(i).toString())) { assign = i; break; }
        ClangFieldToken written = null;
        int depth = 0;
        for (int i = 0; i < assign; i++) {
            ClangNode n = toks.get(i);
            String s = n.toString();
            if ("[".equals(s)) depth++;
            else if ("]".equals(s)) depth--;
            else if (n instanceof ClangFieldToken && depth == 0) written = (ClangFieldToken) n;
        }
        for (ClangNode n : toks)
            if (n instanceof ClangFieldToken) {
                if (n == written) d.fieldsWritten.add(field((ClangFieldToken) n));
                else d.fieldsRead.add(field((ClangFieldToken) n));
            }
    }

    /** Statements as above; field tokens anywhere else (if / while / switch conditions, for headers) are reads. */
    static void walk(ClangNode n, Decompiled d) {
        if (n instanceof ClangStatement) { statement((ClangStatement) n, d); return; }
        if (n instanceof ClangFieldToken) { d.fieldsRead.add(field((ClangFieldToken) n)); return; }
        for (int i = 0; i < n.numChildren(); i++) walk(n.Child(i), d);
    }

    /** The label of the data an address falls in, with the offset into it ("DAT_004ec6ac", "g_table+0x10"). */
    String dataName(Address a) {
        Data d = currentProgram.getListing().getDataContaining(a);
        Address base = d != null ? d.getAddress() : a;
        Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(base);
        String name = s != null ? s.getName() : "DAT_" + base.toString();
        long off = a.subtract(base);
        return off != 0 ? name + "+0x" + Long.toHexString(off) : name;
    }

    @Override
    public void run() throws Exception {
        Path out = Paths.get(getScriptArgs()[0]);
        Path cdir = out.resolve("c");
        Files.createDirectories(cdir);
        try (DirectoryStream<Path> old = Files.newDirectoryStream(cdir, "*.c")) {
            for (Path p : old) Files.delete(p);  // a renamed function leaves no stale file behind
        }
        Listing listing = currentProgram.getListing();
        List<Function> fns = new ArrayList<>();
        for (Function f : listing.getFunctions(true)) if (!f.isExternal()) fns.add(f);

        DecompilerCallback<Decompiled> cb = new DecompilerCallback<>(currentProgram, di -> {
            di.toggleCCode(true);
            di.toggleSyntaxTree(true);
            di.setSimplificationStyle("decompile");
        }) {
            @Override
            public Decompiled process(DecompileResults r, TaskMonitor m) {
                Decompiled d = new Decompiled();
                d.f = r.getFunction();
                DecompiledFunction df = r.getDecompiledFunction();
                if (df == null) {
                    d.error = r.getErrorMessage();
                    d.c = "/* decompile failed: " + d.error + " */\n";
                    return d;
                }
                d.c = df.getC();
                d.signature = df.getSignature();
                if (r.getCCodeMarkup() != null) walk(r.getCCodeMarkup(), d);
                return d;
            }
        };
        cb.setTimeout(120);
        println("decompiling " + fns.size() + " functions");
        List<Decompiled> done = ParallelDecompiler.decompileFunctions(cb, fns, monitor);
        cb.dispose();
        done.removeIf(d -> d == null || d.f == null);
        if (done.size() != fns.size()) println("warning: " + (fns.size() - done.size()) + " functions gave no result");
        done.sort(Comparator.comparing(d -> d.f.getEntryPoint()));

        // Function pointers stored in data (dispatch tables, callbacks): every aligned 4-byte value outside code that
        // equals a function's entry. Tables filled at run time (beyond the image) can't be seen.
        Map<String, List<String>> storedAt = new HashMap<>();
        Set<Address> entries = new HashSet<>();
        for (Function f : fns) entries.add(f.getEntryPoint());
        for (ghidra.program.model.mem.MemoryBlock b : currentProgram.getMemory().getBlocks()) {
            if (!b.isInitialized()) continue;
            for (Address a = b.getStart(); a.compareTo(b.getEnd().subtract(3)) <= 0; a = a.add(4)) {
                if (listing.getInstructionContaining(a) != null) continue;
                long v;
                try { v = currentProgram.getMemory().getInt(a) & 0xffffffffL; } catch (Exception e) { continue; }
                Address t = a.getNewAddress(v);
                if (entries.contains(t)) storedAt.computeIfAbsent(t.toString(), k -> new ArrayList<>()).add(dataName(a) + "@" + a);
            }
        }

        StringBuilder sb = new StringBuilder("{\"functions\":[\n");
        int failed = 0;
        boolean firstFn = true;
        for (Decompiled d : done) {
            Function f = d.f;
            String addr = f.getEntryPoint().toString();
            String file = addr + "_" + f.getName().replaceAll("[^A-Za-z0-9_]", "_") + ".c";
            Files.writeString(cdir.resolve(file), d.c);
            if (d.error != null) failed++;

            // Callees, strings, globals and taken addresses, from the instructions' references.
            Set<String> callees = new TreeSet<>(), strs = new LinkedHashSet<>(), taken = new TreeSet<>();
            Map<String, String> globals = new TreeMap<>();  // name -> r, w, rw, & (address used)
            for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getEntryPoint().toString());
            for (Instruction ins : listing.getInstructions(f.getBody(), true)) {
                for (Reference r : ins.getReferencesFrom()) {
                    Address to = r.getToAddress();
                    if (!to.isMemoryAddress()) continue;
                    RefType t = r.getReferenceType();
                    if (t.isCall() || t.isFlow()) continue;
                    Function target = getFunctionAt(to);
                    if (target != null) { taken.add(to.toString()); continue; }
                    Data data = listing.getDataAt(to);
                    if (data != null && data.hasStringValue()) { strs.add(String.valueOf(data.getValue())); continue; }
                    if (listing.getInstructionContaining(to) != null) continue;  // into code: not a global
                    String name = dataName(to) + "@" + to;
                    String k = t.isWrite() && t.isRead() ? "rw" : t.isWrite() ? "w" : t.isRead() ? "r" : "&";
                    String was = globals.get(name);
                    if (was == null || was.equals("&")) globals.put(name, k);
                    else if (!was.equals(k) && !k.equals("&")) globals.put(name, "rw");
                }
            }
            sb.append(firstFn ? "" : ",\n");
            firstFn = false;
            sb.append(String.format("{\"addr\":\"%s\",\"name\":\"%s\",\"size\":%d,\"thunk\":%b,\"file\":\"%s\",\"signature\":\"%s\",",
                    addr, esc(f.getName()), f.getBody().getNumAddresses(), f.isThunk(), esc(file),
                    esc(d.signature != null ? d.signature.trim() : "")));
            if (d.error != null) sb.append("\"error\":\"").append(esc(d.error)).append("\",");
            sb.append("\"callees\":").append(list(callees));
            sb.append(",\"taken\":").append(list(taken));
            sb.append(",\"stored_at\":").append(list(storedAt.getOrDefault(addr, List.of())));
            sb.append(",\"strings\":").append(list(strs));
            sb.append(",\"globals\":{");
            boolean first = true;
            for (Map.Entry<String, String> e : globals.entrySet()) {
                sb.append(first ? "" : ",").append('"').append(esc(e.getKey())).append("\":\"").append(e.getValue()).append('"');
                first = false;
            }
            sb.append("},\"fields_read\":").append(list(d.fieldsRead));
            sb.append(",\"fields_written\":").append(list(d.fieldsWritten)).append("}");
        }
        sb.append("\n],\"externals\":{");
        boolean firstExt = true;
        for (Function f : currentProgram.getFunctionManager().getExternalFunctions()) {
            sb.append(firstExt ? "" : ",").append(String.format("\"%s\":\"%s::%s\"", f.getEntryPoint(),
                    esc(f.getExternalLocation().getLibraryName()), esc(f.getName())));
            firstExt = false;
        }
        sb.append("}}\n");
        try (FileWriter w = new FileWriter(out.resolve("index.json").toFile())) { w.write(sb.toString()); }
        println("wrote " + done.size() + " decompiles (" + failed + " failed) and index.json to " + out);
    }
}
