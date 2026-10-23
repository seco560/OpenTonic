//@category RETonicTrouble
// CallArgs.java out.txt <target hex> [n]: every call to target with the n instructions before it (default 14).
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.io.FileWriter;
public class CallArgs extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        FileWriter w = new FileWriter(a[0]);
        int n = a.length > 2 ? Integer.parseInt(a[2]) : 14;
        for (Reference r : getReferencesTo(toAddr(a[1]))) {
            if (!r.getReferenceType().isCall()) continue;
            Function f = getFunctionContaining(r.getFromAddress());
            w.write("== " + r.getFromAddress() + " in " + (f == null ? "?" : f.getName()) + "\n");
            Instruction ins = getInstructionAt(r.getFromAddress());
            Instruction[] back = new Instruction[n];
            Instruction p = ins;
            for (int i = 0; i < n && p != null; i++) { p = p.getPrevious(); back[i] = p; }
            for (int i = n - 1; i >= 0; i--) if (back[i] != null) w.write("   " + back[i].getAddress() + "  " + back[i] + "\n");
            w.write("   " + ins.getAddress() + "  " + ins + "\n");
        }
        w.close();
    }
}
