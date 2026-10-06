using System;
using System.Runtime.InteropServices;
public static class DisplayState {
 [DllImport("Magnification.dll")] static extern bool MagInitialize();
 [DllImport("Magnification.dll")] static extern bool MagGetFullscreenColorEffect([Out] float[] matrix);
 [DllImport("Magnification.dll")] static extern bool MagUninitialize();
 [DllImport("powrprof.dll")] static extern uint CallNtPowerInformation(int level,IntPtr input,uint inputSize,out uint output,uint outputSize);
 public static string Read() {
  MagInitialize(); var m=new float[25]; bool ok=MagGetFullscreenColorEffect(m); MagUninitialize();
  uint flags; var status=CallNtPowerInformation(16,IntPtr.Zero,0,out flags,4);
  return "MatrixRead="+ok+" RGB="+m[0]+","+m[6]+","+m[12]+" PowerStatus="+status+" ExecutionFlags="+flags;
 }
}
