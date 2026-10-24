// Argument counts from the asm, to check prototypes before applying them (Ghidra's guesses often merge pushes
// meant for the next call). Per function: its RET (n > 0 = callee cleans, stdcall), then for every direct call the
// first ADD ESP,k within the next 6 instructions (cdecl: k / 4 arguments; a missing ADD = 0 args or merged cleanup).
// Usage: analyzeHeadless <proj_dir> <proj> -process -noanalysis -readOnly -postScript ArgCheck.java <out.txt> <hex addr>...
//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.Reference;

import java.io.FileWriter;
import java.util.*;

public class ArgCheck extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        try (FileWriter w = new FileWriter(a[0])) {
            for (int i = 1; i < a.length; i++) {
                Function f = getFunctionAt(toAddr(a[i]));
                if (f == null) { w.write(a[i] + "\tno function\n"); continue; }
                Set<String> rets = new TreeSet<>();
                for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true))
                    if (ins.getMnemonicString().equals("RET")) rets.add(ins.getNumOperands() > 0 ? ins.getDefaultOperandRepresentation(0) : "0");
                Map<String, Integer> counts = new TreeMap<>();
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    if (!r.getReferenceType().isCall()) continue;
                    Instruction ins = getInstructionAt(r.getFromAddress());
                    String k = "none";
                    for (int n = 0; n < 6 && ins != null; n++) {
                        ins = ins.getNext();
                        if (ins == null) break;
                        if (ins.getMnemonicString().equals("ADD") && ins.getDefaultOperandRepresentation(0).equals("ESP")) {
                            k = String.valueOf(Long.decode(ins.getDefaultOperandRepresentation(1)) / 4);
                            break;
                        }
                        if (ins.getFlowType().isCall() || ins.getFlowType().isJump() || ins.getFlowType().isTerminal()) break;
                    }
                    counts.merge(k, 1, Integer::sum);
                }
                w.write(String.format("%s\t%s\tret %s\tcaller args %s%n", a[i], f.getName(), rets, counts));
            }
        }
    }
}
