// @category WitchTetris
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import java.io.*;

public class DecompileAll extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 2) throw new IllegalArgumentException("Need output C and map paths");
        File outFile = new File(args[0]);
        File mapFile = new File(args[1]);
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        try (PrintWriter out = new PrintWriter(new OutputStreamWriter(new FileOutputStream(outFile), "UTF-8"));
             PrintWriter map = new PrintWriter(new OutputStreamWriter(new FileOutputStream(mapFile), "UTF-8"))) {
            out.println("/* Decompiled from the original working Witch Tetris 3DSX. */");
            out.println("/* Function/variable names are reconstructed and are not the original source names. */\n");
            map.println("address\tname\tsize");
            int count = 0;
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                if (monitor.isCancelled()) break;
                long size = f.getBody().getNumAddresses();
                map.printf("%s\t%s\t%d%n", f.getEntryPoint(), f.getName(), size);
                DecompileResults res = decomp.decompileFunction(f, 45, monitor);
                out.printf("\n/* ===== %s @ %s size=%d ===== */\n", f.getName(), f.getEntryPoint(), size);
                if (res.decompileCompleted() && res.getDecompiledFunction() != null) {
                    out.println(res.getDecompiledFunction().getC());
                } else {
                    out.println("/* decompilation failed: " + res.getErrorMessage() + " */");
                }
                count++;
                if ((count % 100) == 0) println("decompiled " + count + " functions");
            }
            println("total functions: " + count);
        } finally {
            decomp.dispose();
        }
    }
}
