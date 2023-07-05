#include <MesumCore/Kernel/Kernel.hpp>
#include <MesumCore/Kernel/Image.hpp>
#include <MesumGraphics/CrossPlatform.hpp>
#include <MesumGraphics/DearImgui/MesumDearImGui.hpp>
#include <MesumGraphics/RenderTasks/RenderTaskDearImGui.hpp>
#include <MesumGraphics/ApiAbstraction.hpp>

#include "RendererUtils.hpp"
#include "RenderTasksBasicSwapchain.hpp"

#include <iomanip>
#include <random>
#include <algorithm>

#include <MesumGraphics/DearImgui/imgui_internal.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

const m::logging::mChannelID m_Tephigram_ID = mLog_getId();

using namespace m;

ImVec2 operator+(const ImVec2 &a_r, ImVec2 const &a_l)
{
    return {a_r.x + a_l.x, a_r.y + a_l.y};
}

ImVec2 operator-(const ImVec2 &a_r, ImVec2 const &a_l)
{
    return {a_r.x - a_l.x, a_r.y - a_l.y};
}

// temperature °C, pressure kPa
mFloat getPhi(const mFloat a_temperature, const mFloat a_pressure)
{
    return (a_temperature + 273.15) * std::pow((100 / a_pressure), 0.286);
}

class TephigramApp : public m::crossPlatform::IWindowedApplication
{
    void init(m::mCmdLine const &a_cmdLine, void *a_appData) override
    {
        m::crossPlatform::IWindowedApplication::init(a_cmdLine, a_appData);

        // Window setup
        m::mCmdLine const &cmdLine = a_cmdLine;
        m::mUInt           width   = 600;
        m::mUInt           height  = 600;

        m_pDx12Api = new m::dx12::mApi();
        m_pDx12Api->init();
        auto &rDx12Api = *m_pDx12Api;

        m_mainWindow = add_newWindow("Tephigram", width, height, false);

        m_tasksetExecutor.init();

        static const mUInt           s_nbBackBuffer = 3;
        m::render::mISynchTool::Desc desc{s_nbBackBuffer};

        auto &dx12SynchTool = rDx12Api.create_synchTool();
        m_pDx12SynchTool    = &dx12SynchTool;
        dx12SynchTool.init(desc);

        auto &dx12Swapchain = rDx12Api.create_swapchain();
        m_pDx12Swapchain    = &dx12Swapchain;
        m::render::init_swapchainWithWindow(*m_pDx12Api, m_tasksetExecutor,
                                            dx12Swapchain, dx12SynchTool,
                                            *m_mainWindow, s_nbBackBuffer);

        m::dearImGui::init(*m_mainWindow);

        // Render Taskset setup
        m::render::Taskset &taskset_renderPipeline =
            rDx12Api.create_renderTaskset();

        m::render::mTaskDataSwapchainWaitForRT taskData_swapchainWaitForRT{};
        taskData_swapchainWaitForRT.pSwapchain = m_pDx12Swapchain;
        taskData_swapchainWaitForRT.pSynchTool = m_pDx12SynchTool;
        auto &acquireTask = static_cast<m::render::mTaskSwapchainWaitForRT &>(
            taskData_swapchainWaitForRT.add_toTaskSet(taskset_renderPipeline));

        m::render::TaskDataDrawDearImGui taskData_drawDearImGui;
        taskData_drawDearImGui.nbFrames  = s_nbBackBuffer;
        taskData_drawDearImGui.pOutputRT = acquireTask.pOutputRT;
        taskData_drawDearImGui.add_toTaskSet(taskset_renderPipeline);

        m::render::mTaskDataSwapchainPresent taskData_swapchainPresent{};
        taskData_swapchainPresent.pSwapchain = m_pDx12Swapchain;
        taskData_swapchainPresent.pSynchTool = m_pDx12SynchTool;
        taskData_swapchainPresent.add_toTaskSet(taskset_renderPipeline);

        m_tasksetExecutor.confy_permanentTaskset(m::unref_safe(m_pDx12Api),
                                                 taskset_renderPipeline);
        m_mainWindow->attach_toDestroy(m::mCallback<void>(
            [this, &rDx12Api, &taskset_renderPipeline]()
            {
                m_tasksetExecutor.remove_permanentTaskset(
                    rDx12Api, taskset_renderPipeline);
            }));

        m_mainWindow->link_inputManager(&m_inputManager);

        m_inputManager.attach_toKeyEvent(
            m::input::mKeyAction::keyPressed(m::input::keyF11),
            m::input::mKeyActionCallback(
                m_mainWindow, &m::windows::mIWindow::toggle_fullScreen));

        m_inputManager.attach_toKeyEvent(
            m::input::mKeyAction::keyPressed(m::input::keyL),
            m::input::mKeyActionCallback(
                [] { mEnable_logChannels(m_Tephigram_ID); }));

        set_minimalStepDuration(std::chrono::milliseconds(16));
    }

    void destroy() override
    {
        m::crossPlatform::IWindowedApplication::destroy();

        m_pDx12SynchTool->destroy();
        m_pDx12Api->destroy_synchTool(*m_pDx12SynchTool);

        m_pDx12Swapchain->destroy();
        m_pDx12Api->destroy_swapchain(*m_pDx12Swapchain);

        m_pDx12Api->destroy();
        delete m_pDx12Api;

        m::dearImGui::destroy();
    }

    m::mBool step(
        std::chrono::steady_clock::duration const &a_deltaTime) override
    {
        if (!m::crossPlatform::IWindowedApplication::step(a_deltaTime))
        {
            return false;
        }

        static mDouble currentTime = 0.0;
        currentTime +=
            0.001 *
            std::chrono::duration_cast<std::chrono::milliseconds>(a_deltaTime)
                .count();
        if (currentTime > (2.0 * 3.1415))
        {
            currentTime -= 2.0 * 3.1415;
        }

        start_dearImGuiNewFrame(*m_pDx12Api);

        ImGui::NewFrame();

        ImGui::Begin("Simulation Parameters");
        ImGui::Text(
            "FPS: %f",
            1000000.0 / std::chrono::duration_cast<std::chrono::microseconds>(
                            a_deltaTime)
                            .count());
        ImGui::End();

        // Tephigram-----------
        ImGui::Begin("Tephigram Window");

        static mFloat boundTemp[2] = {-30, 30};
        static mFloat boundPhi[2]  = {200, 400};

        ImGui::DragFloat2("Temperature Bounds (°C)", boundTemp, 0.5, -30, 30);
        mFloat      minTemp = boundTemp[0];
        mFloat      maxTemp = boundTemp[1];
        static mInt divTemp = 5;
        ImGui::DragInt("Temperature Subdivisions", &divTemp, 1, 0, 20);

        ImGui::DragFloat2("Temperature Capacity Bounds (°K)", boundPhi, 0.5, 50,
                          700);
        mFloat      minPhi = boundPhi[0];
        mFloat      maxPhi = boundPhi[1];
        static mInt divPhi = 5;
        ImGui::DragInt("Temperature Capacity Subdivisions", &divPhi, 1, 0, 20);

        static mInt nbPressureLine = 5;
        ImGui::DragInt("Nb Pressure Lines", &nbPressureLine, 1, 1, 10);
        static mFloat maxPressure = 100;
        ImGui::DragFloat("Max Pressure (kPa)", &maxPressure, 1, 10, 100);
        static mFloat deltaPressure = 15;
        ImGui::DragFloat("Pressure Delta", &deltaPressure, 1, 1, 30);

        ImGuiContext &G        = *GImGui;
        ImGuiWindow  *window   = G.CurrentWindow;
        ImDrawList   *drawList = ImGui::GetWindowDrawList();
        ImVec2        position = window->DC.CursorPos;

        const ImVec2 canvasSize  = {400, 300};
        const ImVec2 sizePadding = {20, 20};
        const ImVec2 sizeGraph   = canvasSize - sizePadding - sizePadding;
        const ImVec2 graphOrigin =
            position + ImVec2(sizePadding.x, sizePadding.y + sizeGraph.y);
        const ImU32 colCanvas = ImColor(0.95f, 0.95f, 0.85f, 1.0f);
        const ImU32 colBg     = ImColor(0.9f, 0.9f, 0.8f, 1.0f);
        const ImU32 colLine   = ImColor(0.0f, 0.1f, 0.2f, 0.2f);
        const ImU32 colPress  = ImColor(0.0f, 0.1f, 0.2f, 0.4f);


        ImVec2 frameSize = ImGui::CalcItemSize(canvasSize, 400, 300);

        drawList->AddRectFilled(position, position + frameSize, colCanvas);
        drawList->AddRectFilled(position + sizePadding,
                                position + sizePadding + sizeGraph, colBg);

        mFloat yPosNutre = 0;
        mFloat yPosAdded = 0.1 * sizePadding.y;
        mFloat xPosNutre = 0;
        mFloat xPosAdded = -0.5 * sizePadding.x;

        std::vector<std::vector<ImVec2>> lines;
        lines.resize(nbPressureLine);
        for (auto &line : lines) { line.resize(divTemp + 2); }

        for (mUInt i = 0; i <= divTemp; ++i)
        {
            mFloat xPos = sizeGraph.x * i / (divTemp + 1) - 1;
            drawList->AddLine(graphOrigin + ImVec2(xPos, yPosNutre),
                              graphOrigin + ImVec2(xPos, yPosAdded), colLine);

            drawList->AddLine(graphOrigin + ImVec2(xPos, yPosNutre),
                              graphOrigin + ImVec2(xPos, -sizeGraph.y),
                              colLine);

            mFloat temperature = minTemp + (maxTemp - minTemp) * i / (divTemp);
            for (mUInt k = 0; k < nbPressureLine; ++k)
            {
                mFloat  pressure   = maxPressure - k * deltaPressure;
                mDouble tephi      = getPhi(temperature, pressure);
                mDouble tephiRatio = (tephi - minPhi) / (maxPhi - minPhi);
                lines[k][i].x      = graphOrigin.x + xPos;
                lines[k][i].y      = graphOrigin.y - tephiRatio * sizeGraph.y;
            }

            char temp[16];
            ImFormatString(temp, 16, "%d", mInt(temperature));
            drawList->AddText(graphOrigin + ImVec2(xPos, 0.3 * yPosAdded),
                              colLine, temp);
        }

        {
            mFloat temperature = maxTemp + ((maxTemp - minTemp) / (divTemp));
            for (mUInt k = 0; k < nbPressureLine; ++k)
            {
                mFloat  pressure        = maxPressure - k * deltaPressure;
                mDouble tephi           = getPhi(temperature, pressure);
                mDouble tephiRatio      = (tephi - minPhi) / (maxPhi - minPhi);
                lines[k][divTemp + 1].x = graphOrigin.x + sizeGraph.x;
                lines[k][divTemp + 1].y =
                    graphOrigin.y - tephiRatio * sizeGraph.y;
            }
        }

        for (mUInt i = 0; i <= divPhi; ++i)
        {
            mFloat yPos = -sizeGraph.y * i / (divPhi + 1) - 1;
            drawList->AddLine(graphOrigin + ImVec2(xPosNutre, yPos),
                              graphOrigin + ImVec2(xPosAdded, yPos), colLine);

            drawList->AddLine(graphOrigin + ImVec2(xPosNutre, yPos),
                              graphOrigin + ImVec2(sizeGraph.x, yPos), colLine);
        }

        ImGui::PushClipRect(position + sizePadding,
                            position + sizePadding + sizeGraph, true);

        for (auto &line : lines)
        {
            drawList->AddPolyline(line.data(), line.size(), colPress, 0,
                                  1.0f);
        }

        ImGui::End();

        // Render-----------
        ImGui::Render();

        m_tasksetExecutor.run();

        return true;
    }

    m::render::mIApi       *m_pDx12Api;
    m::render::mISwapchain *m_pDx12Swapchain;
    m::render::mISynchTool *m_pDx12SynchTool;

    m::render::mTasksetExecutor m_tasksetExecutor;

    m::input::mCallbackInputManager m_inputManager;
    m::windows::mIWindow           *m_mainWindow;
};

M_EXECUTE_WINDOWED_APP(TephigramApp)
