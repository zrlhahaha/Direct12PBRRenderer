#include "Plateform/Windows/Input.h"

namespace MRenderer
{
    Input::Input()
        : mKeyPressed{}, mMouseDirty(false)
    {
    }

    LRESULT MRenderer::Input::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_LBUTTONDOWN:
            OnMouseMessage(InputKey_LMouseButton, true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONDOWN:
            OnMouseMessage(InputKey_MMouseButton, true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_RBUTTONDOWN:
            OnMouseMessage(InputKey_RMouseButton, true, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_LBUTTONUP:
            OnMouseMessage(InputKey_LMouseButton, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MBUTTONUP:
            OnMouseMessage(InputKey_MMouseButton, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_RBUTTONUP:
            OnMouseMessage(InputKey_RMouseButton, false, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_MOUSEMOVE:
            OnMouseMove(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        case WM_KEYUP:
            OnKeyUp(wParam);
            return 0;
        case WM_KEYDOWN:
            OnKeyDown(wParam);
            return 0;
        default:
            return 0;
        }
    }

    void Input::OnMouseMessage(InputKey key, bool pressed, int x, int y) 
    {
        mKeyPressed[key] = pressed;
        OnMouseMove(x, y);
    }

    void Input::OnMouseMove(int x, int y)
    {
        if (mMouseDirty) UNLIKEYLY
        {
            mLastMousePosition = mMousePosition = Vector2(x, y);
        }
        else 
        {
            mMousePosition.x = x;
            mMousePosition.y = y;
        }
    }

    void Input::OnKeyDown(WPARAM key)
    {
        mKeyPressed[key] = true;
    }

    void Input::OnKeyUp(WPARAM key)
    {
        mKeyPressed[key] = false;
    }
}