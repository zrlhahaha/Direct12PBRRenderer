#include <App.h>
#include "Utils\Console.h"
#include "Resource/DefaultResource.h"
#include "Resource/ResourceLoader.h"


namespace MRenderer
{
    LRESULT CALLBACK
        MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        App* app = App::GetApp();

        return app->MsgProc(hwnd, msg, wParam, lParam);
    }


    App* App::app_ = nullptr;

    App* App::GetApp()
    {
        return app_;
    }

    App::App(HINSTANCE hInstance)
        : mhAppInst(hInstance)
    {
        // Only one D3DApp can be constructed.
        assert(app_ == nullptr);
        app_ = this;

        Console::CreateNewConsole(1024);
        
    }

    App::~App()
    {
        Console::ReleaseConsole();
    }

    HINSTANCE App::AppInst()const
    {
        return mhAppInst;
    }

    HWND App::MainWnd()const
    {
        return mhMainWnd;
    }

    float App::AspectRatio()const
    {
        return static_cast<float>(mClientWidth) / mClientHeight;
    }

    int App::Run()
    {
        try
        {
            return InternalRun();
        }
        catch (std::exception& e)
        {
            std::cerr << "Exception: " << e.what() << std::endl;
        }
        return -1;
    }

    bool App::Initialize()
    {
        std::cout << "Current Working Path: " << std::filesystem::current_path() << std::endl;

        PlateformInitialize();

        if (!InitMainWindow())
            return false;

        // Do the initial resize code.
        OnResize();

        mDevice = std::make_unique<D3D12Device>(mClientWidth, mClientHeight);
        mDevice->BeginFrame();

        mScene = ResourceLoader::Instance().LoadResource<Scene>("Asset/Scene/main.json");

        mCamera = std::make_unique<Camera>(0.4F * PI, mClientWidth, mClientHeight, 0.1F, 1000.0F);
        mCamera->Move(Vector3(0, 0, -5));

        mRenderPipeline = std::make_unique<DeferredRenderPipeline>();
        mRenderScheduler = std::make_unique<RenderScheduler>(mRenderPipeline.get());

        mDevice->EndFrame(); // commit resource creation command
        mCmdExecutor.StartReceivingCommand();

        return true;
    }


    void App::OnResize()
    {
    }

    void App::Update(const GameTimer& gt)
    {
        if (mInput.IsKeyDown(InputKey_RMouseButton)) 
        {
            Vector2 dt = mInput.MouseDeltaPosition() * 0.1F;
            mCamera->Rotate(0, dt.x * Deg2Rad, dt.y * Deg2Rad);
        }

        Vector3 delta_pos;
        if (mInput.IsKeyDown(InputKey_W))
        {
            delta_pos.z += 1;
        }
        if (mInput.IsKeyDown(InputKey_S))
        {
            delta_pos.z += -1;
        }
        if (mInput.IsKeyDown(InputKey_A))
        {
            delta_pos.x += -1;
        }
        if (mInput.IsKeyDown(InputKey_D))
        {
            delta_pos.x += 1;
        }

        delta_pos = mCamera->GetWorldMatrix() * Vector4(delta_pos * 0.05F, 0);
        mCamera->Move(delta_pos);
    }

    void App::Render(const GameTimer& gt)
    {
        mDevice->BeginFrame();
        D3D12CommandList* list = mRenderScheduler->ExecutePipeline(mScene.get(), mCamera.get(), &mTimer);
        mDevice->EndFrame(list);
    }

    LRESULT App::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
            // WM_ACTIVATE is sent when the window is activated or deactivated.  
            // We pause the game when the window is deactivated and unpause it 
            // when it becomes active.  
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                mAppPaused = true;
                mTimer.Pause(true);
            }
            else
            {
                mAppPaused = false;
                mTimer.Pause(false);
            }
            return 0;

            // WM_SIZE is sent when the user resizes the window.  
        case WM_SIZE:
            // Save the new client area dimensions.
            mClientWidth = LOWORD(lParam);
            mClientHeight = HIWORD(lParam);

            if (wParam == SIZE_MINIMIZED)
            {
                mAppPaused = true;
                mMinimized = true;
                mMaximized = false;
            }
            else if (wParam == SIZE_MAXIMIZED)
            {
                mAppPaused = false;
                mMinimized = false;
                mMaximized = true;
                OnResize();
            }
            else if (wParam == SIZE_RESTORED)
            {

                // Restoring from minimized state?
                if (mMinimized)
                {
                    mAppPaused = false;
                    mMinimized = false;
                    OnResize();
                }

                // Restoring from maximized state?
                else if (mMaximized)
                {
                    mAppPaused = false;
                    mMaximized = false;
                    OnResize();
                }
                else if (mResizing)
                {
                    // If user is dragging the resize bars, we do not resize 
                    // the buffers here because as the user continuously 
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is 
                    // done resizing the window and releases the resize bars, which 
                    // sends a WM_EXITSIZEMOVE message.
                }
                else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                {
                    OnResize();
                }
            }
            return 0;

        case WM_PAINT:
            Paint();

            // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
        case WM_ENTERSIZEMOVE:
            mAppPaused = true;
            mResizing = true;
            mTimer.Pause(false);
            return 0;

            // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
            // Here we reset everything based on the new window dimensions.
        case WM_EXITSIZEMOVE:
            mAppPaused = false;
            mResizing = false;
            mTimer.Pause(true);
            OnResize();
            return 0;

            // WM_DESTROY is sent when the window is being destroyed.
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

            // The WM_MENUCHAR message is sent when a menu is active and the user presses 
            // a key that does not correspond to any mnemonic or accelerator key. 
        case WM_MENUCHAR:
            // Don't beep when we alt-enter.
            return MAKELRESULT(0, MNC_CLOSE);

            // Catch this message so to prevent the window from becoming too small.
        case WM_GETMINMAXINFO:
            ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
            ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
            return 0;
        default:
            mInput.HandleMessage(hwnd, msg, wParam, lParam);
        }

        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    bool App::InitMainWindow()
    {
        // register window class for only one time
        static auto ret = [&] {
            WNDCLASS wc;
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = MainWndProc;
            wc.cbClsExtra = 0;
            wc.cbWndExtra = 0;
            wc.hInstance = mhAppInst;
            wc.hIcon = LoadIcon(0, IDI_APPLICATION);
            wc.hCursor = LoadCursor(0, IDC_ARROW);
            wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
            wc.lpszMenuName = 0;
            wc.lpszClassName = "MainWnd";

            if (!RegisterClass(&wc))
            {
                MessageBox(0, "RegisterClass Failed.", 0, 0);
                return false;
            }

            return true;
            }();

        if (!ret)
        {
            return false;
        }


        // Compute window rectangle dimensions based on requested client area dimensions.
        RECT R = { 0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight) };
        AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
        int width = R.right - R.left;
        int height = R.bottom - R.top;

        mhMainWnd = CreateWindow(
            "MainWnd",
            mMainWndCaption.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            width,
            height,
            nullptr,
            nullptr,
            mhAppInst,
            nullptr
        );

        if (!mhMainWnd)
        {
            MessageBox(0, "CreateWindow Failed.", 0, 0);
            return false;
        }

        ShowWindow(mhMainWnd, SW_SHOW);
        UpdateWindow(mhMainWnd);

        return true;
    }

    int App::InternalRun()
    {
        if (! Initialize()) 
        {
            return -1;
        }

        MSG msg = { 0 };
        while (msg.message != WM_QUIT)
        {
            if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

        return (int)msg.wParam;
    }

    void App::Paint()
    {
        if (!mDevice.get()) 
        {
            return;
        }

        mTimer.Tick();
        if (!mPaused)
        {
            mInput.EndMessage();
            UpdateFrameStatus(mRenderScheduler->GetStatus());

            auto future = TaskScheduler::Instance().ExecuteOnMainThread(
                [&]()
                {
                    Update(mTimer);
                    Render(mTimer);
                }
            );
            future.wait();
        }
    }

    void App::UpdateFrameStatus(const FrustumCullStatus& culling_status)
    {
        mPerfromRecord.FrameCount++;

        // Compute averages over one second period.
        const float UpdateInterval = 1.0f;
        if ((mTimer.TotalTime() - mPerfromRecord.TimeElapsed) >= UpdateInterval)
        {
            uint32 fps = static_cast<uint32>(mPerfromRecord.FrameCount / UpdateInterval);

            std::string windowText = mMainWndCaption +
                "    fps: " + std::to_string(fps) +
                "    time" + std::to_string(mTimer.TotalTime()) +
                " culled: " + std::to_string(culling_status.NumCulled) +
                " drawed: " + std::to_string(culling_status.NumDrawCall);

            SetWindowText(mhMainWnd, windowText.c_str());
             
            // Reset for next average.
            mPerfromRecord.FrameCount = 1;
            mPerfromRecord.TimeElapsed = mTimer.TotalTime();
        }
    }
}