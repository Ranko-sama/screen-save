// Historical COM/WMI experiment, pinned to the implementation it measured.
#include "artifacts/memory-optimization/baseline.cpp"
#include <cstdio>
int main() {
    OpenBrightness();
    VARIANT value{};
    CIMTYPE type;
    brightnessArguments->Get(L"Timeout",0,&value,&type,nullptr);
    printf("Timeout vt=%d cim=%d value=%d\n",value.vt,type,value.lVal);
    VariantClear(&value);
    brightnessArguments->Get(L"Brightness",0,&value,&type,nullptr);
    printf("Brightness vt=%d cim=%d\n",value.vt,type);
    VariantClear(&value);
    value.vt=VT_UI1; value.bVal=95;
    printf("Put=%08lx\n",brightnessArguments->Put(L"Brightness",0,&value,0));
    BSTR method=SysAllocString(L"WmiSetBrightness");
    IWbemClassObject* result=nullptr;
    printf("Exec=%08lx\n",wmi->ExecMethod(brightnessMethodPath,method,0,nullptr,brightnessArguments,&result,nullptr));
    if(result){result->Get(L"ReturnValue",0,&value,nullptr,nullptr);printf("Return vt=%d val=%d\n",value.vt,value.lVal);result->Release();}
    SysFreeString(method);
    brightnessArguments->Release();wmi->Release();SysFreeString(brightnessMethodPath);CoUninitialize();
}
