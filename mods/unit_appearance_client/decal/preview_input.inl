// Preview input ownership is independent of asynchronous model geometry.
void AcquirePreviewInput(void* multi){
    if(inputHeld&&preview!=multi)RestoreInput();
    if(editing&&multi&&!inputHeld){preview=multi;previewInput=Read<std::uint8_t>(multi,0x263);previewPitch=Read<std::uint8_t>(multi,0x264);
        Put<std::uint8_t>(multi,0x263,1);Put<std::uint8_t>(multi,0x264,1);inputHeld=true;}
}
void __stdcall PreviewInput(void* control){
    LastErrorGuard error;
    try{std::lock_guard<std::recursive_mutex> hold(gate);
        if(!editing||!inputHeld||control!=preview)return;
        if(pointerOwned&&!placementPointerOwned){Put<std::uint8_t>(control,0x261,0);Put<std::uint8_t>(control,0x262,0);return;}
        const bool right=Read<std::uint8_t>(control,0x261)!=0;
        const bool left=Read<std::uint8_t>(control,0x262)!=0;
        Put<std::uint8_t>(control,0x262,(right||(!placing&&left))?1:0);
    }catch(...){ }
}
