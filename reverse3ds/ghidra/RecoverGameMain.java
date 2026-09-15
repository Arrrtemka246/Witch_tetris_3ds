// @category WitchTetris
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.*;
import java.io.*;

public class RecoverGameMain extends GhidraScript {
    @Override
    protected void run() throws Exception {
        String[] args=getScriptArgs();
        File out=new File(args[0]);
        Address start=toAddr(0x00111f10L);
        Function f=currentProgram.getFunctionManager().getFunctionAt(start);
        if(f==null){
            disassemble(start);
            f=createFunction(start,"witch_main");
        } else {
            f.setName("witch_main", ghidra.program.model.symbol.SourceType.USER_DEFINED);
        }
        println("main="+f+" body="+f.getBody());
        DecompInterface di=new DecompInterface();
        di.toggleCCode(true); di.toggleSyntaxTree(true);
        di.openProgram(currentProgram);
        DecompileResults r=di.decompileFunction(f,300,monitor);
        try(PrintWriter pw=new PrintWriter(new OutputStreamWriter(new FileOutputStream(out),"UTF-8"))){
            pw.println("/* Forced decompilation of original working 3DS game main @ 0x00111F10 */");
            pw.println("/* Reconstructed names/types are approximate. */\n");
            if(r.decompileCompleted() && r.getDecompiledFunction()!=null) pw.println(r.getDecompiledFunction().getC());
            else pw.println("/* failed: "+r.getErrorMessage()+" */");
        }
        di.dispose();
    }
}
