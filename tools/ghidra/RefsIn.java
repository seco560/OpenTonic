//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
public class RefsIn extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs(); FileWriter w = new FileWriter(a[0]);
        long s = Long.parseLong(a[1],16), e = Long.parseLong(a[2],16);
        for (long x = s; x < e; x += 4) {
            for (Reference r : getReferencesTo(toAddr(x))) {
                Function f = getFunctionContaining(r.getFromAddress());
                Instruction ins = getInstructionAt(r.getFromAddress());
                if (ins == null) continue;
                w.write(String.format("%08x <- %s %s  %s%n", x, r.getFromAddress(), f != null ? f.getName() : "?", ins));
            }
        }
        w.close();
    }
}
