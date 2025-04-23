#pragma once
#include <Windows.h>
#include <windowsx.h>

#include "Utils/MathLib.h"

namespace MRenderer 
{
    enum InputKey
    {
        InputKey_LMouseButton	= 0x01,
        InputKey_RMouseButton	= 0x02,
        InputKey_MMouseButton   = 0x03,
        InputKey_0          = 0x30,
        InputKey_1          = 0x31,
        InputKey_2          = 0x32,
        InputKey_3          = 0x33,
        InputKey_4          = 0x34,
        InputKey_5          = 0x35,
        InputKey_6          = 0x36,
        InputKey_7          = 0x37,
        InputKey_8          = 0x38,
        InputKey_9          = 0x39,
        InputKey_A          = 0x41,
        InputKey_B          = 0x42,
        InputKey_C          = 0x43,
        InputKey_D          = 0x44,
        InputKey_E          = 0x45,
        InputKey_F          = 0x46,
        InputKey_G          = 0x47,
        InputKey_H          = 0x48,
        InputKey_I          = 0x49,
        InputKey_J          = 0x4A,
        InputKey_K          = 0x4B,
        InputKey_L          = 0x4C,
        InputKey_M          = 0x4D,
        InputKey_N          = 0x4E,
        InputKey_O          = 0x4F,
        InputKey_P          = 0x50,
        InputKey_Q          = 0x51,
        InputKey_R          = 0x52,
        InputKey_S          = 0x53,
        InputKey_T          = 0x54,
        InputKey_U          = 0x55,
        InputKey_V          = 0x56,
        InputKey_W          = 0x57,
        InputKey_X          = 0x58,
        InputKey_Y          = 0x59,
        InputKey_Z          = 0x5A,
    };



    class Input 
    {
    protected:
        // windows virtual key code range is between 0 to 255
        // ref: https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
        uint32 KeyCount = 1 << CHAR_BIT; 

    public:
        Input();


    public:
        LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

        void EndMessage() 
        {
            mMouseDeltaPosition = mMousePosition - mLastMousePosition;
            mLastMousePosition = mMousePosition;
        }

        inline bool IsKeyDown(InputKey key)
        {
            return mKeyPressed[key];
        }

        inline Vector2 MousePosition() 
        {
            return mMousePosition;
        }

        inline Vector2 MouseDeltaPosition() 
        {
            return mMouseDeltaPosition;
        }

    protected:
        void OnMouseMessage(InputKey key, bool pressed, int x, int y);
        void OnMouseMove(int x, int y);
        void OnKeyDown(WPARAM key);
        void OnKeyUp(WPARAM key);


    protected:
        bool mKeyPressed[1 << CHAR_BIT];
        Vector2 mLastMousePosition;
        Vector2 mMousePosition;
        Vector2 mMouseDeltaPosition;
        bool mMouseDirty;
    };

}

