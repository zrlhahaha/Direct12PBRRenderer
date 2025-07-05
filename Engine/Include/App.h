//***************************************************************************************
// d3dApp.h by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#pragma once
#include <Windows.h>

#include "Renderer/Pipeline/DeferredPipeline.h"
#include "Utils/Time/GameTimer.h"
#include "Renderer/Device/Direct12/D3D12Device.h"
#include "Renderer/Pipeline/DeferredPipeline.h"
#include "Renderer/RenderScheduler.h"
#include "Plateform/Windows/Input.h"
#include "Utils/ConsoleCommand.h"

#include <string>


namespace MRenderer
{
    class App
    {
    public:
        App(HINSTANCE hInstance);
        App(const App&) = delete;
        App& operator=(const App&) = delete;
        virtual ~App();

        static App* GetApp();

        HINSTANCE AppInst()const;
        HWND      MainWnd()const;
        float     AspectRatio()const;

        int Run();

        virtual bool Initialize();
        virtual LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    protected:
        virtual void OnResize();
        virtual void Update(const GameTimer& gt);
        virtual void Render(const GameTimer& gt);

    protected:
        void UpdateFrameStatus(const FrustumCullStatus& culling_status);
        bool InitMainWindow();
        int InternalRun();
        void Paint();

    protected:
        static App* app_;

    protected:
        struct Status
        {
            uint32 FrameCount = 0;
            double TimeElapsed = 0;
        } mPerfromRecord;

        bool mPaused = false;

        HINSTANCE mhAppInst = nullptr; // application instance handle
        HWND      mhMainWnd = nullptr; // main window handle
        bool      mAppPaused = false;  // is the application paused?
        bool      mMinimized = false;  // is the application minimized?
        bool      mMaximized = false;  // is the application maximized?
        bool      mResizing = false;   // are the resize bars being dragged?
        bool      mFullscreenState = false;// fullscreen enabled

        GameTimer mTimer;
        float mRenderTimeStamp = 0;

        Input mInput;

        std::string mMainWndCaption = "MRenderer";
        uint32 mClientWidth = 1440;
        uint32 mClientHeight = 960;

        std::unique_ptr<Camera> mCamera;
        std::unique_ptr<RenderScheduler> mRenderScheduler;
        std::unique_ptr<DeferredRenderPipeline> mRenderPipeline;
        std::shared_ptr<Scene> mScene;
        std::unique_ptr<D3D12Device> mDevice;
        CommandExecutor mCmdExecutor;
    };
}