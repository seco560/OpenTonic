//@category RETonicTrouble
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.FileWriter;
public class Raw extends GhidraScript {
    public void run() throws Exception {
        String[] a = getScriptArgs(); FileWriter w = new FileWriter(a[0]);
        long s = Long.parseLong(a[1],16), e = Long.parseLong(a[2],16);
        for (long x = s; x < e; x += 4) {
            long v = getInt(toAddr(x)) & 0xffffffffL;
            String n = "";
            if (v >= 0x401000L && v < 0x4d0000L) { Function f = getFunctionAt(toAddr(v)); n = f != null ? f.getName() : "code?"; }
            else if (v >= 0x4d0000L && v < 0x520000L) { Data dd = getDataAt(toAddr(v)); n = dd != null && dd.hasStringValue() ? "\"" + dd.getValue() + "\"" : "data"; }
            w.write(String.format("%08x %08x %s%n", x, v, n));
        }
        w.close();
    }
}
