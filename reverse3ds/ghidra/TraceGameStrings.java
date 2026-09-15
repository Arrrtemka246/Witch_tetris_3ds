// @category WitchTetris
import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.*;
import java.io.*;
import java.util.*;

public class TraceGameStrings extends GhidraScript {
    static final String[] NEEDLES = {
        "PHOBOS ROOM","NEW GAME","CUTSCENES","MATRIX","PORN","PAUSED",
        "VTD / VALENTIN","START / SELECT  PAUSE","romfs:/audio/menu_1.mp3",
        "romfs:/audio/phase0.mp3","romfs:/audio/react_pause_hint.mp3"
    };
    private Address findAscii(String s) throws Exception {
        byte[] n=(s+"\0").getBytes("UTF-8");
        Memory m=currentProgram.getMemory();
        Address cur=m.getMinAddress();
        while(cur!=null){
            Address hit=m.findBytes(cur,n,null,true,monitor);
            if(hit==null) return null;
            return hit;
        }
        return null;
    }
    @Override public void run() throws Exception {
        String[] args=getScriptArgs();
        File outFile=new File(args[0]);
        DecompInterface di=new DecompInterface(); di.openProgram(currentProgram);
        Set<Function> funcs=new LinkedHashSet<>();
        try(PrintWriter out=new PrintWriter(new OutputStreamWriter(new FileOutputStream(outFile),"UTF-8"))){
            ReferenceManager rm=currentProgram.getReferenceManager();
            FunctionManager fm=currentProgram.getFunctionManager();
            for(String s:NEEDLES){
                Address a=findAscii(s);
                out.println("\n===== STRING: "+s+" @ "+a+" =====");
                if(a==null) continue;
                ReferenceIterator it=rm.getReferencesTo(a);
                int c=0;
                while(it.hasNext()){
                    Reference r=it.next(); c++;
                    Address from=r.getFromAddress();
                    Function f=fm.getFunctionContaining(from);
                    out.println("ref "+from+" type="+r.getReferenceType()+" func="+(f==null?"<none>":f.getName()+"@"+f.getEntryPoint()));
                    if(f!=null) funcs.add(f);
                }
                out.println("refs="+c);
                // Also scan aligned literal pointers in all initialized memory.
                byte[] ptr=new byte[4]; long av=a.getOffset();
                ptr[0]=(byte)av;ptr[1]=(byte)(av>>8);ptr[2]=(byte)(av>>16);ptr[3]=(byte)(av>>24);
                Address pos=currentProgram.getMemory().getMinAddress();
                int pcount=0;
                while(pos!=null){
                    Address hit=currentProgram.getMemory().findBytes(pos,ptr,null,true,monitor);
                    if(hit==null) break;
                    pcount++;
                    ReferenceIterator rit=rm.getReferencesTo(hit);
                    while(rit.hasNext()){
                        Reference rr=rit.next(); Function ff=fm.getFunctionContaining(rr.getFromAddress());
                        out.println("ptr@"+hit+" used from "+rr.getFromAddress()+" func="+(ff==null?"<none>":ff.getName()+"@"+ff.getEntryPoint()));
                        if(ff!=null) funcs.add(ff);
                    }
                    try { pos=hit.add(1); } catch(Exception ex){ break; }
                }
                out.println("pointer-copies="+pcount);
            }
            out.println("\n\n========= DECOMPILED REFERENCERS =========");
            for(Function f:funcs){
                out.println("\n/* ---- "+f.getName()+" @ "+f.getEntryPoint()+" size="+f.getBody().getNumAddresses()+" ---- */");
                DecompileResults dr=di.decompileFunction(f,90,monitor);
                if(dr.decompileCompleted()&&dr.getDecompiledFunction()!=null) out.println(dr.getDecompiledFunction().getC());
                else out.println("/* FAILED "+dr.getErrorMessage()+" */");
            }
        } finally { di.dispose(); }
    }
}
