// Rename functions from a TSV (addr<TAB>name[<TAB>note]); '#' lines are comments.
// Only default-named (FUN_*) functions are renamed unless the TSV row's name starts with '!'.
// Usage: analyzeHeadless <proj_dir> <proj> -process <exe> -noanalysis -postScript ApplyNames.java <names.tsv>...
//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

import java.nio.file.*;

public class ApplyNames extends GhidraScript {
    @Override
    public void run() throws Exception {
        int renamed = 0, skipped = 0;
        for (String path : getScriptArgs()) {
            for (String line : Files.readAllLines(Paths.get(path))) {
                if (line.isBlank() || line.startsWith("#")) continue;
                String[] cols = line.split("\t");
                Address a = toAddr(Long.parseLong(cols[0], 16));
                Function f = getFunctionAt(a);
                if (f == null) f = createFunction(a, null);
                if (f == null) { skipped++; continue; }
                boolean force = cols[1].startsWith("!");
                String name = force ? cols[1].substring(1) : cols[1];
                if (!force && !f.getName().startsWith("FUN_")) { skipped++; continue; }
                f.setName(name, SourceType.USER_DEFINED);
                if (cols.length > 2) f.setComment(cols[2]);
                renamed++;
            }
        }
        println("renamed " + renamed + ", skipped " + skipped);
    }
}
