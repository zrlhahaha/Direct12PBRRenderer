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

    protected:
        static App* app_;

    protected:
        struct Status
        {
            size_t FrameCount = 0;
            double TimeElapsed = 0;
        } mPerfromRecord;

        bool paused = false;


        HINSTANCE mhAppInst = nullptr; // application instance handle
        HWND      mhMainWnd = nullptr; // main window handle
        bool      mAppPaused = false;  // is the application paused?
        bool      mMinimized = false;  // is the application minimized?
        bool      mMaximized = false;  // is the application maximized?
        bool      mResizing = false;   // are the resize bars being dragged?
        bool      mFullscreenState = false;// fullscreen enabled

        // Set true to use 4X MSAA (?.1.8).  The default is false.
        bool      m4xMsaaState = false;    // 4X MSAA enabled
        UINT      m4xMsaaQuality = 0;      // quality level of 4X MSAA

        GameTimer mTimer;
        float mRenderTimeStamp = 0;

        Input mInput;

        std::string mMainWndCaption = "MRenderer";
        int mClientWidth = 800;
        int mClientHeight = 600;

        std::unique_ptr<Camera> mCamera;
        std::unique_ptr<RenderScheduler> mRenderScheduler;
        std::unique_ptr<DeferredRenderPipeline> mRenderPipeline;
        std::unique_ptr<D3D12Device> mDevice;
        std::shared_ptr<Scene> mScene;
        CommandExecutor mCmdExecutor;
    };
}