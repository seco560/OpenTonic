//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import ghidra.app.decompiler.*;
import java.io.FileWriter;
public class MkDis extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        FileWriter w = new FileWriter(a[0]);
        DecompInterface di = new DecompInterface();
        di.openProgram(currentProgram);
        for (int i = 1; i < a.length; i++) {
            try {
                Address ad = toAddr(a[i]);
                Function f = getFunctionAt(ad);
                if (f == null) {
                    if (getInstructionAt(ad) == null) disassemble(ad);
                    f = createFunction(ad, null);
                    if (f == null) f = getFunctionContaining(ad);
                }
                if (f == null) { w.write("=== " + a[i] + " no function\n"); continue; }
                w.write("=== " + a[i] + " " + f.getName() + "\n");
                DecompileResults res = di.decompileFunction(f, 60, monitor);
                w.write(res.getDecompiledFunction() != null ? res.getDecompiledFunction().getC() : "decompile failed\n");
            } catch (Exception e) { w.write("=== ERR " + a[i] + " " + e + "\n"); }
            w.flush();
        }
        w.close();
    }
}
