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
#include <numbers>

#include <MesumGraphics/DearImgui/imgui_internal.h>

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

const m::logging::mChannelID m_Tephigram_ID = mLog_getId();

using namespace m;

static const mFloat g_k   = 0.286f;
static const mFloat g_c2k = 273.15f;

ImVec2 operator+(ImVec2 const &a_r, ImVec2 const &a_l)
{
    return {a_r.x + a_l.x, a_r.y + a_l.y};
}

ImVec2 operator-(ImVec2 const &a_r, ImVec2 const &a_l)
{
    return {a_r.x - a_l.x, a_r.y - a_l.y};
}

// temperature °C, pressure kPa
mFloat get_phi(mFloat const a_temperature, mFloat const a_pressure)
{
    return (a_temperature + 273.15) * std::pow((100 / a_pressure), g_k);
}

mFloat get_pressure(mFloat const a_temperature, mFloat const a_phi)
{
    return 100 / std::pow(a_phi / (a_temperature + g_c2k), 1 / g_k);
}

mFloat get_tempFromPos(ImVec2 const &a_position,
                       ImVec2 const &a_boundsTemperature,
                       ImVec2 const &a_sizeGraph, mFloat a_angleGraph)
{
    mFloat xTemp = a_position.x - (a_position.y - 0.5 * a_sizeGraph.y) *
                                      std::tan(a_angleGraph);
    return a_boundsTemperature.x +
           (xTemp / a_sizeGraph.x) *
               (a_boundsTemperature.y - a_boundsTemperature.x);
}

mFloat get_phiFromPos(ImVec2 const &a_position, ImVec2 const &a_boundsPhi,
                      ImVec2 const &a_sizeGraph, mFloat a_angleGraph)
{
    mFloat xPhi = a_position.y +
                  (a_position.x - 0.5 * a_sizeGraph.x) * std::tan(a_angleGraph);
    return a_boundsPhi.x +
           (xPhi / a_sizeGraph.y) * (a_boundsPhi.y - a_boundsPhi.x);
}

mFloat get_yFromXandPressure(mFloat const a_x, mFloat const a_pressure,
                             ImVec2 const &a_boundsTemperature,
                             ImVec2 const &a_boundsPhi,
                             ImVec2 const &a_sizeGraph, mFloat a_angleGraph)
{
    mFloat b      = 0.5 * a_sizeGraph.y;
    mFloat b2     = 0.5 * a_sizeGraph.x;
    mFloat sx     = a_sizeGraph.x;
    mFloat sy     = a_sizeGraph.y;
    mFloat mt     = a_boundsTemperature.x + g_c2k;
    mFloat mphi   = a_boundsPhi.x;
    mFloat dt     = a_boundsTemperature.y - a_boundsTemperature.x;
    mFloat dphi   = a_boundsPhi.y - a_boundsPhi.x;
    mFloat tan    = std::tan(a_angleGraph);
    mFloat invTan = 1 / tan;
    mFloat p      = std::pow(100 / a_pressure, g_k);
    mFloat temperature =
        (mphi + (b + (a_x - b2) * tan + invTan * (mt * sx / dt + a_x)) * (dphi/sy)) /
        (p + (dphi/sy) * invTan * sx / dt);
    return -invTan * ((temperature - mt) * sx / dt - a_x) + b;
}

class TephigramApp : public m::crossPlatform::IWindowedApplication
{
    void init(m::mCmdLine const &a_cmdLine, void *a_appData) override
    {
        m::crossPlatform::IWindowedApplication::init(a_cmdLine, a_appData);

        // Window setup
        m::mCmdLine const &cmdLine = a_cmdLine;
        m::mUInt           width   = 1280;
        m::mUInt           height  = 720;

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
        if (currentTime > (2.0 * std::numbers::pi))
        {
            currentTime -= 2.0 * std::numbers::pi;
        }

        start_dearImGuiNewFrame(*m_pDx12Api);

        ImGui::NewFrame();

        ImGui::DockSpaceOverViewport(ImGui::GetMainViewport());

        ImGui::Begin("Application info");
        ImGui::Text(
            "FPS: %f",
            1000000.0 / std::chrono::duration_cast<std::chrono::microseconds>(
                            a_deltaTime)
                            .count());

        static ImVec2 mousePos{0, 0};
        ImGui::Text("MousePos: %f:%f", mousePos.x, mousePos.y);
        static mFloat cursorTemp = 0.0f;
        ImGui::Text("Temp @ cursor (°C): %f", cursorTemp);
        static mFloat cursorPhi = 0.0f;
        ImGui::Text("Temp Capacity @ cursor (K): %f", cursorPhi);
        static mFloat cursorPressure = 0.0f;
        ImGui::Text("Pressure @ cursor (kPa): %f", cursorPressure);

        ImGui::End();

        // Tephigram-----------
        ImGui::Begin("Tephigram Parameters");

        static mFloat boundTemp[2] = {-40, 15};
        static mFloat boundPhi[2]  = {285, 345};

        ImGui::DragFloat2("Temperature Bounds (°C)", boundTemp, 0.5, -50, 50);
        mFloat      minTemp = boundTemp[0];
        mFloat      maxTemp = boundTemp[1];
        static mInt divTemp = 5;
        ImGui::DragInt("Temperature Subdivisions", &divTemp, 1, 0, 20);
        mFloat deltaTemp = (maxTemp - minTemp) / (divTemp + 1);

        ImGui::DragFloat2("Temperature Capacity Bounds (°K)", boundPhi, 0.5, 50,
                          700);
        mFloat      minPhi = boundPhi[0];
        mFloat      maxPhi = boundPhi[1];
        static mInt divPhi = 5;
        ImGui::DragInt("Temperature Capacity Subdivisions", &divPhi, 1, 0, 20);
        mFloat deltaPhi = (maxPhi - minPhi) / (divPhi + 1);

        static mInt nbPressureLine = 10;
        ImGui::DragInt("Nb Pressure Lines", &nbPressureLine, 1, 1, 10);
        static mFloat maxPressure = 100;
        ImGui::DragFloat("Max Pressure (kPa)", &maxPressure, 1, 10, 100);
        static mFloat deltaPressure = 10;
        ImGui::DragFloat("Pressure Delta", &deltaPressure, 1, 1, 30);

        static mFloat roation = 0.25;
        ImGui::DragFloat("grid angle(rad)", &roation, 0.01, 0.0, 0.45);
        mFloat angle = std::numbers::pi * roation;

        ImGui::End();

        ImGui::Begin("Tephigram");
        ImGuiContext &G        = *GImGui;
        ImGuiWindow  *window   = G.CurrentWindow;
        ImDrawList   *drawList = ImGui::GetWindowDrawList();
        ImVec2        position = window->DC.CursorPos;

        const ImVec2 canvasSize  = {600, 700};
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
        mFloat xPosNutre = 0;

        // Clip rect
        ImGui::PushClipRect(position + sizePadding,
                            position + sizePadding + sizeGraph, true);

        mFloat tiltX             = std::tan(angle) * (0.5 * sizeGraph.y);
        mInt   additionalDivTemp = tiltX / deltaTemp;
        mFloat sizeHorizontal    = sizeGraph.x / (divTemp + 1);
        // Crooked
        for (mInt i = -additionalDivTemp; i <= (divTemp + additionalDivTemp);
             ++i)
        {
            mFloat xPos = i * sizeHorizontal;
            mFloat tilt = std::tan(angle) * (0.5 * sizeGraph.y);

            drawList->AddLine(graphOrigin + ImVec2(xPos - tilt, yPosNutre),
                              graphOrigin + ImVec2(xPos + tilt, -sizeGraph.y),
                              colLine);

            mFloat temperature = minTemp + deltaTemp * i;

            char string[16];
            ImFormatString(string, 16, "%d", mInt(temperature));
            drawList->AddText(graphOrigin + ImVec2(xPos, -(0.5 * sizeGraph.y)),
                              colLine, string);
        }

        mFloat tiltY            = std::tan(angle) * (0.5 * sizeGraph.x);
        mInt   additionalDivPhi = tiltY / deltaPhi;
        mFloat sizeVertical     = sizeGraph.y / (divPhi + 1);
        for (mInt i = -additionalDivPhi; i <= (divPhi + additionalDivPhi); ++i)
        {
            mFloat yPos = -sizeVertical * i;
            mFloat tilt = std::tan(angle) * (0.5 * sizeGraph.x);

            drawList->AddLine(graphOrigin + ImVec2(xPosNutre, yPos - tilt),
                              graphOrigin + ImVec2(sizeGraph.x, yPos + tilt),
                              colLine);

            mFloat phi = minPhi + deltaPhi * i;

            char string[16];
            ImFormatString(string, 16, "%d", mInt(phi));
            drawList->AddText(
                graphOrigin + ImVec2((0.5 * sizeGraph.x) + 5, yPos - 5),
                colLine, string);
        }

        // Cursor data
        mousePos = ImVec2(ImGui::GetMousePos().x - graphOrigin.x,
                          graphOrigin.y - ImGui::GetMousePos().y);
        cursorTemp =
            get_tempFromPos(mousePos, {minTemp, maxTemp}, sizeGraph, angle);
        cursorPhi =
            get_phiFromPos(mousePos, {minPhi, maxPhi}, sizeGraph, angle);
        cursorPressure = get_pressure(cursorTemp, cursorPhi);

        // Pressure Lines
        std::vector<std::vector<ImVec2>> lines;
        lines.resize(nbPressureLine);
        for (auto &line : lines)
        {
            line.resize(divTemp + 2 * additionalDivTemp + 2);
        }
        for (mInt i = -additionalDivTemp;
             i <= (divTemp + additionalDivTemp) + 1; ++i)
        {
            mFloat temperature = minTemp + deltaTemp * i;
            for (mUInt k = 0; k < nbPressureLine; ++k)
            {
                mFloat  pressure   = maxPressure - k * deltaPressure;
                mDouble tephi      = get_phi(temperature, pressure);
                mFloat  tephiRatio = (tephi - minPhi) / (maxPhi - minPhi);

                ImVec2 oT{i * sizeHorizontal, (-0.5f * sizeGraph.y)};
                ImVec2 oPhi{0.5f * sizeGraph.x, -tephiRatio * sizeGraph.y};

                mFloat alphaT   = std::sin(angle);
                mFloat alphaPhi = std::cos(angle);
                mFloat betaT    = -std::cos(angle);
                mFloat betaPhi  = std::sin(angle);

                mFloat tPhi = 0;
                if (alphaT == 0)
                {
                    lines[k][i + additionalDivTemp].x = graphOrigin.x + oT.x;
                    lines[k][i + additionalDivTemp].y = graphOrigin.y + oPhi.y;
                }
                else
                {
                    tPhi =
                        (oT.y + (betaT / alphaT) * (oPhi.x - oT.x) - oPhi.y) /
                        (betaPhi - betaT * (alphaPhi / alphaT));

                    lines[k][i + additionalDivTemp].x =
                        graphOrigin.x + oPhi.x + alphaPhi * tPhi;
                    lines[k][i + additionalDivTemp].y =
                        graphOrigin.y + oPhi.y + betaPhi * tPhi;
                }
            }
        }

        for (mUInt k = 0; k < nbPressureLine; ++k)
        {
            mFloat pressure = maxPressure - k * deltaPressure;
            mFloat x        = (divTemp / 4) * sizeHorizontal;
            mFloat y =
                get_yFromXandPressure(x, pressure, {minTemp, maxTemp},
                                      {minPhi, maxPhi}, sizeGraph, angle);
            {
                char string[16];
                ImFormatString(string, 16, "p:%d", mInt(pressure));
                drawList->AddText(ImVec2(graphOrigin.x + x, graphOrigin.y - y),
                                  colLine, string);
            }
        }

        ImGui::PushClipRect(position + sizePadding,
                            position + sizePadding + sizeGraph, true);

        for (auto &line : lines)
        {
            drawList->AddPolyline(line.data(), line.size(), colPress, 0, 1.0f);
        }

        ImGui::PopClipRect();

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
