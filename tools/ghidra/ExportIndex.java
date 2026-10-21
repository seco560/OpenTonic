// Dump a JSON index of the program: every function with its body size, callees,
// and the defined strings it references. Consumed by tools/exe/modmap.py.
// Usage: analyzeHeadless <proj_dir> <proj> -process <exe> -postScript ExportIndex.java <out.json>
//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.util.*;

public class ExportIndex extends GhidraScript {
    private static String esc(String s) {
        StringBuilder b = new StringBuilder();
        for (char c : s.toCharArray()) {
            if (c == '"' || c == '\\') b.append('\\').append(c);
            else if (c < 0x20 || c > 0x7e) b.append(String.format("\\u%04x", (int) c));
            else b.append(c);
        }
        return b.toString();
    }

    @Override
    public void run() throws Exception {
        String out = getScriptArgs()[0];
        Listing listing = currentProgram.getListing();
        StringBuilder sb = new StringBuilder("{\"functions\":[\n");
        boolean first = true;
        for (Function f : listing.getFunctions(true)) {
            if (monitor.isCancelled()) break;
            Set<String> callees = new TreeSet<>();
            for (Function c : f.getCalledFunctions(monitor)) callees.add(c.getEntryPoint().toString());
            Set<String> strs = new LinkedHashSet<>();
            for (Address a : f.getBody().getAddresses(true)) {
                for (Reference r : getReferencesFrom(a)) {
                    Data d = listing.getDataAt(r.getToAddress());
                    if (d != null && d.hasStringValue()) strs.add(esc(String.valueOf(d.getValue())));
                }
            }
            if (!first) sb.append(",\n");
            first = false;
            sb.append(String.format("{\"addr\":\"%s\",\"name\":\"%s\",\"size\":%d,\"thunk\":%b,\"external\":%b,\"callees\":[",
                    f.getEntryPoint(), esc(f.getName()), f.getBody().getNumAddresses(), f.isThunk(), f.isExternal()));
            sb.append(String.join(",", callees.stream().map(s -> "\"" + s + "\"").toList()));
            sb.append("],\"strings\":[");
            sb.append(String.join(",", strs.stream().map(s -> "\"" + s + "\"").toList()));
            sb.append("]}");
        }
        for (Function f : currentProgram.getFunctionManager().getExternalFunctions()) {
            String lib = f.getExternalLocation().getLibraryName();
            sb.append(String.format(",\n{\"addr\":\"%s\",\"name\":\"%s::%s\",\"size\":0,\"thunk\":false,\"external\":true,\"callees\":[],\"strings\":[]}",
                    f.getEntryPoint(), esc(lib), esc(f.getName())));
        }
        sb.append("\n]}\n");
        try (FileWriter w = new FileWriter(out)) { w.write(sb.toString()); }
        println("wrote " + out);
    }
}
