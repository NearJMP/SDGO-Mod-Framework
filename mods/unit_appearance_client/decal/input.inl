// Shared production window input path, also exercised by the window-message regression.
bool LogicalPoint(HWND window,LPARAM lparam,float& x,float& y){
    RECT rect{};if(!GetClientRect(window,&rect)||rect.right<=0||rect.bottom<=0)return false;
    x=GET_X_LPARAM(lparam)*1024.f/rect.right;y=GET_Y_LPARAM(lparam)*768.f/rect.bottom;return true;
}
LRESULT CALLBACK InputProc(HWND window,UINT message,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR){
    if(message==WM_NCDESTROY){pointerOwned=placementPointerOwned=false;dragging=mouseDown=false;page.PointerUp();RemoveWindowSubclass(window,&InputProc,inputSubclass);inputWindow=nullptr;return DefSubclassProc(window,message,wp,lp);}
    if(message==WM_KILLFOCUS||message==WM_CANCELMODE||message==WM_CAPTURECHANGED){
        std::lock_guard<std::recursive_mutex> hold(gate);const bool owned=pointerOwned;page.PointerUp();pointerOwned=placementPointerOwned=false;dragging=mouseDown=false;
        if(message!=WM_CAPTURECHANGED&&owned&&GetCapture()==window)ReleaseCapture();
    }
    if(message==WM_MOUSEWHEEL){std::lock_guard<std::recursive_mutex> hold(gate);POINT point{GET_X_LPARAM(lp),GET_Y_LPARAM(lp)};ScreenToClient(window,&point);RECT rect{};GetClientRect(window,&rect);
        if(rect.right>0&&rect.bottom>0&&page.Scroll(point.x*1024.f/rect.right,point.y*768.f/rect.bottom,GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA))return 0;}
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK||message==WM_MOUSEMOVE||message==WM_LBUTTONUP){
        std::lock_guard<std::recursive_mutex> hold(gate);float x=0,y=0;
        if(editing&&page.Visible()&&LogicalPoint(window,lp,x,y)){
            if(message==WM_LBUTTONDOWN||message==WM_LBUTTONDBLCLK){
                const auto command=page.PointerDown(x,y);ApplyCommand(command);
                if(page.Captured()){pointerOwned=true;if(GetCapture()!=window)SetCapture(window);return 0;}
                if(placing&&!armed&&!(wp&MK_RBUTTON)&&PreviewContains(x,y)){
                    placementPointerOwned=pointerOwned=mouseDown=true;
                    if(GetCapture()!=window)SetCapture(window);
                    dragging=PlaceAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));return 0;
                }
            }else if(message==WM_MOUSEMOVE){
                if(placementPointerOwned){
                    if(placing&&!armed&&(wp&MK_LBUTTON)&&!(wp&MK_RBUTTON)&&PreviewContains(x,y))dragging=PlaceAt(GET_X_LPARAM(lp),GET_Y_LPARAM(lp));
                    return 0;
                }
                const auto command=page.PointerMove(x,y);if(pointerOwned){ApplyCommand(command);return 0;}
            }
            else if(pointerOwned){
                const auto command=placementPointerOwned?ui::Command{}:page.PointerRelease(x,y);
                page.PointerUp();pointerOwned=placementPointerOwned=false;dragging=mouseDown=false;
                if(GetCapture()==window)ReleaseCapture();
                if(command.action==ui::Action::choose||command.action==ui::Action::select)ApplyCommand(command);
                else if(command.action!=ui::Action::none)Action(command.action);
                return 0;
            }
        }else if(pointerOwned){page.PointerUp();pointerOwned=placementPointerOwned=false;dragging=mouseDown=false;if(GetCapture()==window)ReleaseCapture();return 0;}
    }
    return DefSubclassProc(window,message,wp,lp);
}
