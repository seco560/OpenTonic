//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.FileWriter;
public class InsnGrep extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs();
        FileWriter w = new FileWriter(a[0]);
        InstructionIterator it = currentProgram.getListing().getInstructions(true);
        while (it.hasNext()) {
            Instruction ins = it.next();
            String s = ins.toString();
            for (int i = 1; i < a.length; i++) if (s.contains(a[i])) {
                Function f = getFunctionContaining(ins.getAddress());
                w.write(ins.getAddress() + " " + (f == null ? "?" : f.getName()) + "  " + s + "\n");
            }
        }
        w.close();
    }
}
