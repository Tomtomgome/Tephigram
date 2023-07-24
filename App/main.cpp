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
static const mFloat g_eps = 0.622f;  // R'/Rv

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

// temperature °C, pressure kPa, ws g/kg
mFloat get_pressureFromWandTemperature(mFloat const a_ws,
                                       mFloat const a_temperature)
{
    return ((1000 * g_eps - a_ws) / (10 * a_ws)) * 6.112 *
           std::exp((17.67 * a_temperature / (a_temperature + 243.5)));
}

// temperature °C, pressure kPa, ws g/kg
mFloat get_wsFromTemperatureAndPressure(mFloat const a_temperature,
                                        mFloat const a_pressure)
{
    return 1000 * g_eps *
           (6.112 * std::exp(17.67 * a_temperature / (243.5 + a_temperature))) /
           (a_pressure * 10 - (6.112 * std::exp(17.67 * a_temperature /
                                                (243.5 + a_temperature))));
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

ImVec2 get_posFromTempAndPhi(mFloat const a_temperature, mFloat const a_phi,
                             ImVec2 const &a_boundsTemperature,
                             ImVec2 const &a_boundsPhi,
                             ImVec2 const &a_sizeGraph, mFloat a_angleGraph)
{
    mFloat sx     = a_sizeGraph.x;
    mFloat sy     = a_sizeGraph.y;
    mFloat mt     = a_boundsTemperature.x;
    mFloat mphi   = a_boundsPhi.x;
    mFloat dt     = a_boundsTemperature.y - a_boundsTemperature.x;
    mFloat dphi   = a_boundsPhi.y - a_boundsPhi.x;
    mFloat tan    = std::tan(a_angleGraph);
    mFloat invTan = 1 / tan;

    mFloat x = (tan / (tan * tan + 1)) *
               ((sy / dphi) * (a_phi - mphi) - 0.5 * sy + 0.5 * sx +
                (invTan * sx / dt) * (a_temperature - mt));
    mFloat y = -invTan * ((sx / dt) * (a_temperature - mt) - x) + 0.5 * sy;

    return {x, y};
}

// temperature °C, pressure kPa
ImVec2 get_posFromWandTemperature(mFloat const a_ws, mFloat const a_temperature,
                                  ImVec2 const &a_boundsTemperature,
                                  ImVec2 const &a_boundsPhi,
                                  ImVec2 const &a_sizeGraph,
                                  mFloat        a_angleGraph)
{
    mFloat p = 10 * ((g_eps - a_ws) / a_ws) * 6.112 *
               std::exp((17.67 * a_temperature / (a_temperature + 243.5)));
    mFloat phi = get_phi(a_temperature, p);

    return get_posFromTempAndPhi(a_temperature, phi, a_boundsTemperature,
                                 a_boundsPhi, a_sizeGraph, a_angleGraph);
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
        (mphi +
         (b + (a_x - b2) * tan + invTan * (mt * sx / dt + a_x)) * (dphi / sy)) /
        (p + (dphi / sy) * invTan * sx / dt);
    return -invTan * ((temperature - mt) * sx / dt - a_x) + b;
}

struct GridParameters
{
    mFloat boundTemp[2]{-40, 15};  // °C
    mInt   divTemp{5};
    mFloat boundPhi[2]{285, 345};  // °K
    mInt   divPhi{5};

    mFloat rotation{0.25};  // rad

    void expose_dearImGui();
};

void GridParameters::expose_dearImGui()
{
    if (ImGui::TreeNode("Grid Parameters"))
    {
        ImGui::DragFloat2("Temperature Bounds (°C)", boundTemp, 0.5, -50, 50);
        ImGui::DragInt("Temperature Subdivisions", &divTemp, 1, 0, 20);
        ImGui::DragFloat2("Temperature Capacity Bounds (°K)", boundPhi, 0.5, 50,
                          700);
        ImGui::DragInt("Temperature Capacity Subdivisions", &divPhi, 1, 0, 20);
        ImGui::DragFloat("grid angle(rad)", &rotation, 0.01, 0.0, 0.45);
        ImGui::TreePop();
    }
}

struct PressureLineParameters
{
    mInt   nbPressureLine{10};
    mFloat maxPressure{100};  // kPa
    mFloat deltaPressure{10};

    mBool showPressureLine{true};

    void expose_dearImGui();
};

void PressureLineParameters::expose_dearImGui()
{
    if (ImGui::TreeNode("Pressure Lines"))
    {
        ImGui::Checkbox("Show Pressure Lines", &showPressureLine);

        ImGui::DragInt("Nb Pressure Lines", &nbPressureLine, 1, 1, 10);
        ImGui::DragFloat("Max Pressure (kPa)", &maxPressure, 1, 10, 100);
        ImGui::DragFloat("Pressure Delta", &deltaPressure, 1, 1, 30);

        ImGui::TreePop();
    }
}

struct VaporLineParameters
{
    mInt                nbVaporLines{10};
    std::vector<mFloat> wss{1.0f, 1.5f,  2.0f,  3.0f,  5.0f,
                            7.0f, 10.0f, 15.0f, 20.0f, 30.0f};
    mBool               showVaporLines{true};

    void expose_dearImGui();
};

void VaporLineParameters::expose_dearImGui()
{
    if (ImGui::TreeNode("Vapor Lines"))
    {
        ImGui::Checkbox("Show vapor lines", &showVaporLines);

        ImGui::DragInt("Nb Vapor lines", &nbVaporLines, 1, 1, 10);
        if (wss.size() != nbVaporLines)
        {
            wss.resize(nbVaporLines);
        }

        for (mUInt i = 0; i < wss.size(); ++i)
        {
            char name[16];
            ImFormatString(name, 16, "ws %d", mInt(i));
            ImGui::DragFloat(name, &wss[i], 0.01f, 0.1f, 100.0f);
        }

        ImGui::TreePop();
    }
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
        static mFloat waterSaturationRatio = 0.0f;
        ImGui::Text("Water Sat rat @ cursor (g/kg): %f", waterSaturationRatio);

        ImGui::End();

        // Tephigram-----------
        ImGui::Begin("Tephigram Parameters");

        m_gp.expose_dearImGui();

        mFloat minTemp   = m_gp.boundTemp[0];
        mFloat maxTemp   = m_gp.boundTemp[1];
        mFloat deltaTemp = (maxTemp - minTemp) / (m_gp.divTemp + 1);
        mFloat minPhi    = m_gp.boundPhi[0];
        mFloat maxPhi    = m_gp.boundPhi[1];
        mFloat deltaPhi  = (maxPhi - minPhi) / (m_gp.divPhi + 1);
        mFloat angle     = std::numbers::pi * m_gp.rotation;

        m_plp.expose_dearImGui();
        m_vlp.expose_dearImGui();

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
        const ImU32 colLine   = ImColor(0.0f, 0.1f, 0.2f, 0.7f);
        const ImU32 colPress  = ImColor(0.0f, 0.1f, 0.2f, 0.2f);
        const ImU32 colVapor  = ImColor(0.0f, 0.6f, 0.2f, 0.2f);

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
        mFloat sizeHorizontal    = sizeGraph.x / (m_gp.divTemp + 1);
        mInt   additionalDivTemp = tiltX / sizeHorizontal;
        // Crooked
        for (mInt i = -additionalDivTemp;
             i <= (m_gp.divTemp + additionalDivTemp); ++i)
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
        mFloat sizeVertical     = sizeGraph.y / (m_gp.divPhi + 1);
        mInt   additionalDivPhi = tiltY / sizeVertical;
        for (mInt i = -additionalDivPhi; i <= (m_gp.divPhi + additionalDivPhi);
             ++i)
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

        waterSaturationRatio =
            get_wsFromTemperatureAndPressure(cursorTemp, cursorPressure);

        ImGui::PushClipRect(position + sizePadding,
                            position + sizePadding + sizeGraph, true);
        // Pressure Lines
        if (m_plp.showPressureLine)
        {
            std::vector<std::vector<ImVec2>> lines;
            lines.resize(m_plp.nbPressureLine);
            for (auto &line : lines)
            {
                line.resize(m_gp.divTemp + 2 * additionalDivTemp + 2);
            }
            for (mInt i = -additionalDivTemp;
                 i <= (m_gp.divTemp + additionalDivTemp) + 1; ++i)
            {
                mFloat temperature = minTemp + deltaTemp * i;
                for (mUInt k = 0; k < m_plp.nbPressureLine; ++k)
                {
                    mFloat pressure =
                        m_plp.maxPressure - k * m_plp.deltaPressure;
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
                        lines[k][i + additionalDivTemp].x =
                            graphOrigin.x + oT.x;
                        lines[k][i + additionalDivTemp].y =
                            graphOrigin.y + oPhi.y;
                    }
                    else
                    {
                        tPhi = (oT.y + (betaT / alphaT) * (oPhi.x - oT.x) -
                                oPhi.y) /
                               (betaPhi - betaT * (alphaPhi / alphaT));

                        lines[k][i + additionalDivTemp].x =
                            graphOrigin.x + oPhi.x + alphaPhi * tPhi;
                        lines[k][i + additionalDivTemp].y =
                            graphOrigin.y + oPhi.y + betaPhi * tPhi;
                    }
                }
            }

            for (mUInt k = 0; k < m_plp.nbPressureLine; ++k)
            {
                mFloat pressure = m_plp.maxPressure - k * m_plp.deltaPressure;
                mFloat x        = (m_gp.divTemp / 4) * sizeHorizontal;
                mFloat y =
                    get_yFromXandPressure(x, pressure, {minTemp, maxTemp},
                                          {minPhi, maxPhi}, sizeGraph, angle);
                {
                    char string[16];
                    ImFormatString(string, 16, "p:%d", mInt(pressure));
                    drawList->AddText(
                        ImVec2(graphOrigin.x + x, graphOrigin.y - y), colLine,
                        string);
                }
            }

            for (auto &line : lines)
            {
                drawList->AddPolyline(line.data(), line.size(), colPress, 0,
                                      1.0f);
            }
        }

        if (m_vlp.showVaporLines)
        {
            // Vapor lines
            std::vector<std::vector<ImVec2>> vaporLines;
            vaporLines.resize(m_vlp.nbVaporLines);
            for (auto &line : vaporLines)
            {
                line.resize(m_gp.divTemp + 2 * additionalDivTemp + 2);
            }
            for (mInt i = -additionalDivTemp;
                 i <= (m_gp.divTemp + additionalDivTemp) + 1; ++i)
            {
                mFloat temperature = minTemp + deltaTemp * i;
                for (mUInt k = 0; k < m_vlp.nbVaporLines; ++k)
                {
                    mFloat ws = m_vlp.wss[k];
                    //                ImVec2 position =
                    //                get_posFromWandTemperature(
                    //                    ws, temperature, {minTemp, maxTemp},
                    //                    {minPhi, maxPhi}, sizeGraph, angle);
                    mFloat pressure =
                        get_pressureFromWandTemperature(ws, temperature);
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
                        vaporLines[k][i + additionalDivTemp].x =
                            graphOrigin.x + oT.x;
                        vaporLines[k][i + additionalDivTemp].y =
                            graphOrigin.y + oPhi.y;
                    }
                    else
                    {
                        tPhi = (oT.y + (betaT / alphaT) * (oPhi.x - oT.x) -
                                oPhi.y) /
                               (betaPhi - betaT * (alphaPhi / alphaT));

                        vaporLines[k][i + additionalDivTemp].x =
                            graphOrigin.x + oPhi.x + alphaPhi * tPhi;
                        vaporLines[k][i + additionalDivTemp].y =
                            graphOrigin.y + oPhi.y + betaPhi * tPhi;
                    }
                }
            }

            for (auto &line : vaporLines)
            {
                drawList->AddPolyline(line.data(), line.size(), colVapor, 0,
                                      1.0f);
            }
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

    GridParameters         m_gp;
    PressureLineParameters m_plp;
    VaporLineParameters    m_vlp;
};

M_EXECUTE_WINDOWED_APP(TephigramApp)
