// Type the Ghidra project from tools/exe (docs/TYPES.md): the structs of types.h (category /TT), the prototypes of
// signatures.tsv, the globals of globals.tsv; then export the typed decompiles of every listed function.
// Safe to re-run: types are replaced in place, prototypes re-applied, labels and comments not duplicated.
// Nothing is changed unless Ghidra's layout of types.h matches every AT() / SIZE() line of types_check.c.
// Usage (dry run: add -readOnly; changes are then discarded):
//   analyzeHeadless files/out/_decomp_artifacts/ghidra TonicTrouble -process -noanalysis -scriptPath tools/ghidra \
//     -postScript ApplyTypes.java tools/exe files/out/_decomp_artifacts/typed
//@category RETonicTrouble
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.FunctionRenameOption;
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;

import java.io.FileWriter;
import java.nio.file.*;
import java.util.*;
import java.util.regex.*;

public class ApplyTypes extends GhidraScript {
    static final CategoryPath TT = new CategoryPath("/TT"), TT_FN = new CategoryPath("/TT/fn");
    // Names Ghidra or tools/exe/modmap.py made up (FUN_x, MEC_x, GAM_PlayAnim_x): a prototype's name may replace them.
    static final Pattern AUTO_NAME = Pattern.compile("(FUN|[A-Z]{2,5}(_[A-Za-z0-9]+)?)_[0-9a-f]{8}");

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path dir = Paths.get(args[0]), out = Paths.get(args[1]);
        Files.createDirectories(out);
        DataTypeManager dtm = currentProgram.getDataTypeManager();

        // 1. Parse and check, without touching the program.
        StandAloneDataTypeManager tmp = new StandAloneDataTypeManager("tt", dtm.getDataOrganization());
        int tx = tmp.startTransaction("parse");
        CParser parser = new CParser(tmp, true, null);
        parser.parse(stripComments(Files.readString(dir.resolve("types.h"))));
        if (!parser.didParseSucceed()) throw new Exception("types.h: " + parser.getParseMessages());
        List<DataType> parsed = new ArrayList<>();
        for (DataType dt : collect(tmp)) {
            // a function pointer typedef comes with a function definition of the same name: rename that one, or the
            // signature parser finds two and resolves neither
            if (dt instanceof FunctionDefinition && !dt.getName().endsWith("_def")) dt.setName(dt.getName() + "_def");
            dt.setCategoryPath(dt instanceof FunctionDefinition ? TT_FN : TT);
            parsed.add(dt);
        }
        tmp.endTransaction(tx, true);
        List<String> bad = checkLayout(tmp, Files.readAllLines(dir.resolve("types_check.c")));
        if (!bad.isEmpty()) throw new Exception("layout differs from types_check.c:\n  " + String.join("\n  ", bad));
        println("types.h: " + parsed.size() + " types, layout matches types_check.c");

        // 2. Types into /TT (replacing earlier versions of the same names).
        for (DataType dt : parsed) {
            for (DataType other : findByName(dtm, dt.getName()))
                if (!other.getCategoryPath().getPath().startsWith("/TT")) println("note: " + dt.getName() + " also exists as " + other.getPathName());
            dtm.addDataType(dt, DataTypeConflictHandler.REPLACE_HANDLER);
        }
        // types.h no longer defines them: remove (their uses were all re-applied from the tables above)
        Set<String> keep = new HashSet<>();
        for (DataType dt : parsed) keep.add(dt.getCategoryPath().getPath() + "/" + dt.getName());
        for (CategoryPath cp : List.of(TT, TT_FN)) {
            Category cat = dtm.getCategory(cp);
            if (cat == null) continue;
            for (DataType dt : cat.getDataTypes())
                if (!keep.contains(cp.getPath() + "/" + dt.getName()) && !(dt instanceof Pointer) && !(dt instanceof Array)) {
                    println("remove stale " + dt.getPathName());
                    dtm.remove(dt, monitor);
                }
        }
        try (FileWriter w = new FileWriter(out.resolve("layout.txt").toFile())) {
            for (DataType dt : parsed) dumpLayout(w, dtm.getDataType(dt.getCategoryPath(), dt.getName()));
        }

        // 3. Prototypes.
        sigParser = new FunctionSignatureParser(dtm, null);
        List<Function> listed = new ArrayList<>();
        Set<Long> fromSignatures = new HashSet<>();
        for (String line : Files.readAllLines(dir.resolve("signatures.tsv"))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] c = line.split("\t");
            Function f = applyPrototype(toAddr(Long.parseLong(c[0], 16)), c[1], c.length > 2 ? c[2] : null, AUTO_NAME);
            if (f != null) { listed.add(f); fromSignatures.add(f.getEntryPoint().getOffset()); }
        }
        println("prototypes: " + applied + " applied, " + renamed + " renamed");

        // 3b. Dispatch tables: one prototype for every entry (tables.tsv), names from ai_names.tsv.
        List<Function> tableFns = applyTables(dir, fromSignatures);
        println("tables: " + tableFns.size() + " functions typed");

        // 4. Globals.
        int labelled = 0;
        Pattern decl = Pattern.compile("(\\w+)\\s*(\\**)\\s*(\\w+)\\s*(?:\\[(\\d+)\\])?");
        for (String line : Files.readAllLines(dir.resolve("globals.tsv"))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] c = line.split("\t");
            Address a = toAddr(Long.parseLong(c[0], 16));
            Matcher m = decl.matcher(c[1].trim());
            if (!m.matches()) { println("bad declaration: " + c[1]); continue; }
            DataType t = builtinOrTT(dtm, m.group(1));
            if (t == null) { println("unknown type " + m.group(1)); continue; }
            for (int k = 0; k < m.group(2).length(); k++) t = new PointerDataType(t, dtm);
            if (m.group(4) != null) t = new ArrayDataType(t, Integer.parseInt(m.group(4)), t.getLength(), dtm);
            if (c.length > 2 && !c[2].equals("-")) {
                float v = getFloat(a), e = Float.parseFloat(c[2]);
                if (Math.abs(v - e) > 1e-6 * Math.max(1, Math.abs(e))) {
                    println("skip " + m.group(3) + " @" + c[0] + ": memory holds " + v + ", expected " + e);
                    continue;
                }
            }
            Address end = a.add(t.getLength() - 1);
            if (getInstructionContaining(a) != null || getInstructionContaining(end) != null) {
                println("skip " + m.group(3) + ": code at " + c[0]); continue;
            }
            clearListing(a, end);
            createData(a, t);
            Symbol s = getSymbolAt(a);
            if (s == null || !s.getName().equals(m.group(3))) createLabel(a, m.group(3), true, SourceType.USER_DEFINED);
            if (c.length > 3) setPlateComment(a, c[3]);
            labelled++;
        }
        println("globals: " + labelled);

        // 5. Typed decompiles, one file per listed function.
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (Function f : listed) {
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            String text = r.getDecompiledFunction() != null ? r.getDecompiledFunction().getC() : "/* decompile failed: " + r.getErrorMessage() + " */\n";
            writeDecompile(out, f, text);
        }
        Path aiOut = out.resolve("ai");
        Files.createDirectories(aiOut);
        for (Function f : tableFns) {
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            String text = r.getDecompiledFunction() != null ? r.getDecompiledFunction().getC() : "/* decompile failed: " + r.getErrorMessage() + " */\n";
            writeDecompile(aiOut, f, text);
        }
        di.dispose();
        println("wrote " + listed.size() + " decompiles to " + out + ", " + tableFns.size() + " to " + aiOut);
    }


    FunctionSignatureParser sigParser;
    int applied = 0, renamed = 0;

    // Apply a prototype at a; the prototype's name replaces the current one only when it matches renamable.
    Function applyPrototype(Address a, String text, String note, Pattern renamable) throws Exception {
        Function f = getFunctionAt(a);
        if (f == null) {
            if (getInstructionAt(a) == null) disassemble(a);
            f = createFunction(a, null);
        }
        if (f == null) { println("skip " + a + ": no function"); return null; }
        // the parser wants "T* name": "IdCard *f(" reads as a name "*f"
        String proto = text.replaceAll("\\s*\\*", "*").replaceAll("\\*(?=\\w)", "* ");
        FunctionDefinitionDataType def = sigParser.parse(null, proto);
        def.setCallingConvention("__cdecl");
        if (!f.getName().equals(def.getName()) && !renamable.matcher(f.getName()).matches()) {
            println("keep name " + f.getName() + " (not " + def.getName() + ")");
            def.setName(f.getName());
        }
        ApplyFunctionSignatureCmd cmd = new ApplyFunctionSignatureCmd(a, def, SourceType.USER_DEFINED, false, false,
                DataTypeConflictHandler.DEFAULT_HANDLER, FunctionRenameOption.RENAME);
        String before = f.getName();
        if (!cmd.applyTo(currentProgram, monitor)) { println("FAILED " + a + ": " + cmd.getStatusMsg()); return null; }
        applied++;
        if (!before.equals(f.getName())) renamed++;
        if (note != null && !note.isBlank()) addComment(f, note);
        return f;
    }

    static final Pattern TABLE_RENAMABLE = Pattern.compile("(FUN|AI)_[0-9a-f]{8}|(NODE|KW|COND|FUNC|OP|PROC|META|FIELD)_\\w+_[0-9a-f]{8}");

    List<Function> applyTables(Path dir, Set<Long> skip) throws Exception {
        Map<String, String[]> names = new HashMap<>();   // "KIND index" -> {name, notes}
        for (String line : Files.readAllLines(dir.resolve("ai_names.tsv"))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] c = line.split("\t");
            if (c.length >= 3) names.put(c[0] + " " + c[1], new String[] { c[2], c.length > 4 ? c[4] : "" });
        }
        Map<Long, List<String[]>> targets = new TreeMap<>();   // address -> {label, kind, index, prototype, suffix}
        for (String line : Files.readAllLines(dir.resolve("tables.tsv"))) {
            if (line.isBlank() || line.startsWith("#")) continue;
            String[] c = line.split("\t");
            long base = Long.parseLong(c[1], 16);
            int count = Integer.parseInt(c[2]), stride = Integer.parseInt(c[3]), off = Integer.parseInt(c[4]);
            for (int i = 0; i < count; i++) {
                long p = getInt(toAddr(base + (long) i * stride + off)) & 0xffffffffL;
                if (p < 0x401000L || p >= 0x4d0000L) { println(c[0] + " " + i + ": not code (" + Long.toHexString(p) + ")"); continue; }
                targets.computeIfAbsent(p, k -> new ArrayList<>())
                       .add(new String[] { c[0], c[6], String.valueOf(i), c[5], c.length > 7 ? c[7] : "" });
            }
        }
        List<Function> done = new ArrayList<>();
        for (Map.Entry<Long, List<String[]>> e : targets.entrySet()) {
            Address a = toAddr(e.getKey());
            List<String[]> uses = e.getValue();
            StringBuilder note = new StringBuilder();
            for (String[] u : uses) {
                String[] n = names.get(u[1] + " " + u[2]);
                note.append(note.length() > 0 ? "; " : "").append(u[0]).append(' ').append(u[2]);
                if (n != null) note.append(' ').append(n[0]);
            }
            if (skip.contains(e.getKey())) continue;
            Set<String> protos = new HashSet<>();
            for (String[] u : uses) protos.add(u[3]);
            Function f = getFunctionAt(a);
            if (protos.size() > 1 || (f != null && !TABLE_RENAMABLE.matcher(f.getName()).matches())) {
                if (f != null) addComment(f, "table entry: " + note);
                println("comment only " + a + " " + (f == null ? "?" : f.getName()) + ": " + note);
                continue;
            }
            String[] first = uses.get(0);
            String[] n = names.get(first[1] + " " + first[2]);
            String what = n != null ? n[0].replaceAll("[^A-Za-z0-9]", "") : first[2];
            boolean shared = uses.stream().map(u -> u[2]).distinct().count() > 1;
            String name = first[1] + "_" + what + (shared ? "Etc" : "") + first[4] + "_" + String.format("%08x", e.getKey());
            if (n != null && n[0].contains("?")) note.append(" (name inferred)");
            Function g = applyPrototype(a, first[3].replace("{name}", name), note.toString(), TABLE_RENAMABLE);
            if (g != null) done.add(g);
        }
        return done;
    }

    // <addr>_<name>.c; an earlier file of the same address under another name (renamed since) is replaced
    static void writeDecompile(Path dir, Function f, String text) throws Exception {
        String prefix = f.getEntryPoint().toString() + "_", file = prefix + f.getName() + ".c";
        try (DirectoryStream<Path> old = Files.newDirectoryStream(dir, prefix + "*.c")) {
            for (Path p : old) if (!p.getFileName().toString().equals(file)) Files.delete(p);
        }
        Files.writeString(dir.resolve(file), text);
    }

    static String stripComments(String s) {
        return s.replaceAll("(?s)/\\*.*?\\*/", " ").replaceAll("//[^\n]*", "");
    }

    static List<DataType> collect(DataTypeManager m) {
        List<DataType> l = new ArrayList<>();
        m.getAllDataTypes().forEachRemaining(dt -> {
            if (dt instanceof Composite || dt instanceof TypeDef || dt instanceof FunctionDefinition) l.add(dt);
        });
        return l;
    }

    static List<DataType> findByName(DataTypeManager m, String name) {
        List<DataType> l = new ArrayList<>();
        m.findDataTypes(name, l);
        return l;
    }

    static DataType builtinOrTT(DataTypeManager m, String name) {
        DataType t = m.getDataType(TT, name);
        if (t != null) return t;
        switch (name) {
            case "float": return FloatDataType.dataType;
            case "char": return CharDataType.dataType;
            case "int": return IntegerDataType.dataType;
            default: return null;
        }
    }

    // types_check.c lines: AT(T, a.b, off); SIZE(T, n);
    static List<String> checkLayout(DataTypeManager m, List<String> lines) {
        List<String> bad = new ArrayList<>();
        Pattern at = Pattern.compile("^AT\\((\\w+), ([\\w.]+), (0x[0-9A-Fa-f]+|\\d+)\\);");
        Pattern size = Pattern.compile("^SIZE\\((\\w+), (0x[0-9A-Fa-f]+|\\d+)\\);");
        int n = 0;
        for (String line : lines) {
            Matcher a = at.matcher(line), s = size.matcher(line);
            if (s.find()) {
                n++;
                DataType t = m.getDataType(TT, s.group(1));
                long want = Long.decode(s.group(2));
                if (t == null) bad.add("missing " + s.group(1));
                else if (t.getLength() != want) bad.add(s.group(1) + " size " + hex(t.getLength()) + ", want " + hex(want));
            } else if (a.find()) {
                n++;
                DataType t = m.getDataType(TT, a.group(1));
                long want = Long.decode(a.group(3)), off = 0;
                for (String field : a.group(2).split("\\.")) {
                    if (t instanceof TypeDef td) t = td.getBaseDataType();
                    DataTypeComponent hit = null;
                    if (t instanceof Composite comp)
                        for (DataTypeComponent dc : comp.getDefinedComponents())
                            if (field.equals(dc.getFieldName())) hit = dc;
                    if (hit == null) { off = -1; break; }
                    off += hit.getOffset();
                    t = hit.getDataType();
                }
                if (off != want) bad.add(a.group(1) + "." + a.group(2) + " at " + hex(off) + ", want " + hex(want));
            }
        }
        if (n == 0) bad.add("no checks found");
        return bad;
    }

    static String hex(long v) { return v < 0 ? "missing" : "0x" + Long.toHexString(v); }

    static void dumpLayout(FileWriter w, DataType dt) throws Exception {
        if (!(dt instanceof Composite c)) return;
        w.write(dt.getName() + " " + hex(dt.getLength()) + "\n");
        for (DataTypeComponent dc : c.getDefinedComponents())
            w.write(String.format("  +0x%03x %-22s %s%n", dc.getOffset(), dc.getFieldName(), dc.getDataType().getDisplayName()));
    }

    void addComment(Function f, String note) {
        String old = f.getComment();
        if (old == null || old.isBlank()) f.setComment(note);
        else if (!old.contains(note)) f.setComment(note + "\n" + old);
    }
}
