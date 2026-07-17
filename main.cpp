#include "header.h"
#include <SDL.h>

/*
NOTE : You are free to change the code as you wish, the main objective is to make the
       application work and pass the audit.

       It will be provided the main function with the following functions :

       - `void systemWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the system window on your screen
       - `void memoryProcessesWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the memory and processes window on your screen
       - `void networkWindow(const char *id, ImVec2 size, ImVec2 position)`
            This function will draw the network window on your screen
*/

// About Desktop OpenGL function loaders:
//  Modern desktop OpenGL doesn't have a standard portable header file to load OpenGL function pointers.
//  Helper libraries are often used for this purpose! Here we are supporting a few common ones (gl3w, glew, glad).
//  You may use another loader/header of your choice (glext, glLoadGen, etc.), or chose to manually implement your own.
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
#include <GL/gl3w.h> // Initialize with gl3wInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
#include <GL/glew.h> // Initialize with glewInit()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
#include <glad/glad.h> // Initialize with gladLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
#include <glad/gl.h> // Initialize with gladLoadGL(...) or gladLoaderLoadGL()
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
#define GLFW_INCLUDE_NONE      // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/Binding.h> // Initialize with glbinding::Binding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
#define GLFW_INCLUDE_NONE        // GLFW including OpenGL headers causes ambiguity or multiple definition errors.
#include <glbinding/glbinding.h> // Initialize with glbinding::initialize()
#include <glbinding/gl/gl.h>
using namespace gl;
#else
#include IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#endif

static const int kPlotSamples = 120;

struct LivePlotState
{
    float samples[kPlotSamples];
    int writeIndex;
    bool filled;
    bool paused;
    float fps;
    float yMax;
    double lastPushSeconds;
    float latest;

    LivePlotState(float defaultFps, float defaultYMax)
        : writeIndex(0), filled(false), paused(false),
          fps(defaultFps), yMax(defaultYMax), lastPushSeconds(0.0), latest(0.0f)
    {
        for (int i = 0; i < kPlotSamples; ++i)
            samples[i] = 0.0f;
    }

    void maybePush(float value, double nowSeconds)
    {
        latest = value;
        if (paused)
            return;

        const float safeFps = (fps < 1.0f) ? 1.0f : fps;
        const double interval = 1.0 / static_cast<double>(safeFps);
        if (lastPushSeconds > 0.0 && (nowSeconds - lastPushSeconds) < interval)
            return;

        lastPushSeconds = nowSeconds;
        samples[writeIndex] = value;
        writeIndex = (writeIndex + 1) % kPlotSamples;
        if (writeIndex == 0)
            filled = true;
    }

    void drawControls(float fpsMin, float fpsMax, float yMin, float yMaxLimit)
    {
        ImGui::Checkbox("Pause graph", &paused);
        ImGui::SliderFloat("FPS", &fps, fpsMin, fpsMax, "%.0f");
        ImGui::SliderFloat("Y scale", &yMax, yMin, yMaxLimit, "%.0f");
        if (yMax < yMin)
            yMax = yMin;
    }

    void drawPlot(const char *plotId, const char *overlay)
    {
        const int count = filled ? kPlotSamples : writeIndex;
        if (count <= 0)
        {
            ImGui::TextDisabled("Waiting for samples...");
            return;
        }

        // Plot chronologically: oldest -> newest
        float ordered[kPlotSamples];
        const int start = filled ? writeIndex : 0;
        for (int i = 0; i < count; ++i)
            ordered[i] = samples[(start + i) % kPlotSamples];

        ImGui::PlotLines(plotId, ordered, count, 0, overlay, 0.0f, yMax, ImVec2(-1.0f, 100.0f));
    }
};

// systemWindow, display information for the system monitorization
void systemWindow(const char *id, ImVec2 size, ImVec2 position)
{
    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    const string user = readLoggedInUser();
    const string host = readHostname();
    const string cpuModel = readCpuModelName();
    const TaskCounts tasks = countTasksByState();

    ImGui::Text("Operating System : %s", getOsName());
    ImGui::Text("Logged-in user   : %s", user.c_str());
    ImGui::Text("Hostname         : %s", host.c_str());
    ImGui::Text("CPU              : %s", cpuModel.c_str());
    ImGui::Separator();
    ImGui::Text("Tasks : %d total", tasks.total);
    ImGui::Text("  running          : %d", tasks.running);
    ImGui::Text("  sleeping         : %d", tasks.sleeping);
    ImGui::Text("  uninterruptible  : %d", tasks.uninterruptible);
    ImGui::Text("  zombie           : %d", tasks.zombie);
    ImGui::Text("  traced/stopped   : %d", tasks.stopped);
    ImGui::Text("  idle             : %d", tasks.idle);

    ImGui::Separator();
    if (ImGui::BeginTabBar("SystemResourceTabs"))
    {
        const double now = ImGui::GetTime();

        if (ImGui::BeginTabItem("CPU"))
        {
            static LivePlotState cpuPlot(30.0f, 100.0f);
            const float usage = sampleCpuUsagePercent();
            cpuPlot.maybePush(usage, now);

            char overlay[64];
            snprintf(overlay, sizeof(overlay), "%.1f%%", cpuPlot.latest);

            cpuPlot.drawControls(1.0f, 60.0f, 10.0f, 200.0f);
            cpuPlot.drawPlot("##CpuUsagePlot", overlay);
            ImGui::Text("Current CPU usage: %.1f%%", cpuPlot.latest);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Fan"))
        {
            static LivePlotState fanPlot(10.0f, 5000.0f);
            const FanReading fan = readFanState();
            const float speed = fan.available ? static_cast<float>(fan.speedRpm) : 0.0f;
            fanPlot.maybePush(speed, now);

            if (!fan.available)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Fan sensors not available on this machine");
            else
            {
                ImGui::Text("Status : %s", fan.enabled ? "enabled / active" : "disabled");
                ImGui::Text("Speed  : %d RPM", fan.speedRpm);
                ImGui::Text("Level  : %s", fan.level.c_str());
            }

            char overlay[64];
            if (fan.available)
                snprintf(overlay, sizeof(overlay), "%d RPM", fan.speedRpm);
            else
                snprintf(overlay, sizeof(overlay), "n/a");

            fanPlot.drawControls(1.0f, 60.0f, 500.0f, 10000.0f);
            fanPlot.drawPlot("##FanSpeedPlot", overlay);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thermal"))
        {
            static LivePlotState thermalPlot(10.0f, 100.0f);
            const ThermalReading thermal = readThermalState();
            const float temp = thermal.available ? thermal.celsius : 0.0f;
            thermalPlot.maybePush(temp, now);

            if (!thermal.available)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f),
                                   "Thermal sensors not available on this machine");

            char overlay[64];
            if (thermal.available)
                snprintf(overlay, sizeof(overlay), "%.1f C", thermalPlot.latest);
            else
                snprintf(overlay, sizeof(overlay), "n/a");

            thermalPlot.drawControls(1.0f, 60.0f, 20.0f, 120.0f);
            thermalPlot.drawPlot("##ThermalPlot", overlay);
            if (thermal.available)
                ImGui::Text("Current temperature: %.1f C", thermalPlot.latest);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// memoryProcessesWindow, display information for the memory and processes information
void memoryProcessesWindow(const char *id, ImVec2 size, ImVec2 position)
{
    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    const MemSnapshot mem = readMemSnapshot();
    const DiskSnapshot disk = readDiskSnapshot("/");

    const float ramFrac = (mem.totalRamMB > 0.0f) ? (mem.usedRamMB / mem.totalRamMB) : 0.0f;
    const float swapFrac = (mem.totalSwapMB > 0.0f) ? (mem.usedSwapMB / mem.totalSwapMB) : 0.0f;
    const float diskFrac = (disk.totalGB > 0.0f) ? (disk.usedGB / disk.totalGB) : 0.0f;

    char ramOverlay[64];
    snprintf(ramOverlay, sizeof(ramOverlay), "%.1f / %.1f MB", mem.usedRamMB, mem.totalRamMB);
    ImGui::Text("Physical Memory (RAM)");
    ImGui::ProgressBar(ramFrac, ImVec2(-1.0f, 18.0f), ramOverlay);

    char swapOverlay[64];
    snprintf(swapOverlay, sizeof(swapOverlay), "%.1f / %.1f MB", mem.usedSwapMB, mem.totalSwapMB);
    ImGui::Text("Virtual Memory (SWAP)");
    ImGui::ProgressBar(swapFrac, ImVec2(-1.0f, 18.0f), swapOverlay);

    char diskOverlay[64];
    snprintf(diskOverlay, sizeof(diskOverlay), "%.1f / %.1f GB", disk.usedGB, disk.totalGB);
    ImGui::Text("Disk (/)");
    ImGui::ProgressBar(diskFrac, ImVec2(-1.0f, 18.0f), diskOverlay);

    ImGui::Separator();

    if (ImGui::BeginTabBar("ProcessTabs"))
    {
        if (ImGui::BeginTabItem("Processes"))
        {
            static char filterBuf[128] = "";
            static std::map<int, bool> selected;

            ImGui::InputText("Filter", filterBuf, sizeof(filterBuf));
            ImGui::SameLine();
            if (ImGui::Button("Clear selection"))
                selected.clear();

            // Refresh process list about twice per second
            static vector<ProcessRow> cached;
            static double lastRefresh = 0.0;
            const double now = ImGui::GetTime();
            if (cached.empty() || (now - lastRefresh) >= 0.5)
            {
                cached = collectProcessRows();
                lastRefresh = now;
            }

            const ImGuiTableFlags flags =
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;

            if (ImGui::BeginTable("ProcessTable", 5, flags, ImVec2(-1.0f, -1.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("PID");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("State");
                ImGui::TableSetupColumn("CPU usage");
                ImGui::TableSetupColumn("Memory usage");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < cached.size(); ++i)
                {
                    const ProcessRow &proc = cached[i];
                    char pidText[32];
                    snprintf(pidText, sizeof(pidText), "%d", proc.pid);

                    if (filterBuf[0] != '\0')
                    {
                        const bool nameHit = proc.name.find(filterBuf) != string::npos;
                        const bool pidHit = string(pidText).find(filterBuf) != string::npos;
                        if (!nameHit && !pidHit)
                            continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);

                    const bool isSelected = selected[proc.pid];
                    char label[64];
                    snprintf(label, sizeof(label), "%d##proc%d", proc.pid, proc.pid);
                    if (ImGui::Selectable(label, isSelected,
                                          ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowItemOverlap))
                    {
                        // Toggle so multiple rows can stay selected
                        selected[proc.pid] = !isSelected;
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(proc.name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(proc.state.c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.1f%%", proc.cpuPercent);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%.1f%%", proc.memPercent);
                }

                ImGui::EndTable();
            }

            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// network, display information network information
void networkWindow(const char *id, ImVec2 size, ImVec2 position)
{
    ImGui::Begin(id);
    ImGui::SetWindowSize(id, size);
    ImGui::SetWindowPos(id, position);

    const Networks nets = collectIpv4Addresses();
    const vector<NetIfaceStats> ifaces = collectNetIfaceStats();
    const double twoGiB = 2.0 * 1024.0 * 1024.0 * 1024.0;

    ImGui::Text("IPv4 addresses");
    if (nets.ip4s.empty())
        ImGui::TextDisabled("No IPv4 interfaces found");
    else
    {
        for (size_t i = 0; i < nets.ip4s.size(); ++i)
        {
            const IP4 &ip = nets.ip4s[i];
            ImGui::BulletText("%s : %s", ip.name.c_str(), ip.addressBuffer);
        }
    }

    ImGui::Separator();

    if (ImGui::BeginTabBar("NetCounterTabs"))
    {
        if (ImGui::BeginTabItem("RX"))
        {
            if (ImGui::BeginTable("RxTable", 9,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                  ImVec2(-1.0f, 140.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Interface");
                ImGui::TableSetupColumn("bytes");
                ImGui::TableSetupColumn("packets");
                ImGui::TableSetupColumn("errs");
                ImGui::TableSetupColumn("drop");
                ImGui::TableSetupColumn("fifo");
                ImGui::TableSetupColumn("frame");
                ImGui::TableSetupColumn("compressed");
                ImGui::TableSetupColumn("multicast");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < ifaces.size(); ++i)
                {
                    const NetIfaceStats &n = ifaces[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(n.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", (unsigned long long)n.rx.bytes);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", (unsigned long long)n.rx.packets);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", (unsigned long long)n.rx.errs);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", (unsigned long long)n.rx.drop);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%llu", (unsigned long long)n.rx.fifo);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%llu", (unsigned long long)n.rx.frame);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text("%llu", (unsigned long long)n.rx.compressed);
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text("%llu", (unsigned long long)n.rx.multicast);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("TX"))
        {
            if (ImGui::BeginTable("TxTable", 9,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                      ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                                  ImVec2(-1.0f, 140.0f)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Interface");
                ImGui::TableSetupColumn("bytes");
                ImGui::TableSetupColumn("packets");
                ImGui::TableSetupColumn("errs");
                ImGui::TableSetupColumn("drop");
                ImGui::TableSetupColumn("fifo");
                ImGui::TableSetupColumn("colls");
                ImGui::TableSetupColumn("carrier");
                ImGui::TableSetupColumn("compressed");
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < ifaces.size(); ++i)
                {
                    const NetIfaceStats &n = ifaces[i];
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(n.name.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%llu", (unsigned long long)n.tx.bytes);
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%llu", (unsigned long long)n.tx.packets);
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%llu", (unsigned long long)n.tx.errs);
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%llu", (unsigned long long)n.tx.drop);
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%llu", (unsigned long long)n.tx.fifo);
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text("%llu", (unsigned long long)n.tx.colls);
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text("%llu", (unsigned long long)n.tx.carrier);
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text("%llu", (unsigned long long)n.tx.compressed);
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::Separator();
    ImGui::Text("Usage bars (scale 0 - 2 GB)");

    if (ImGui::BeginTabBar("NetUsageTabs"))
    {
        if (ImGui::BeginTabItem("RX"))
        {
            for (size_t i = 0; i < ifaces.size(); ++i)
            {
                const NetIfaceStats &n = ifaces[i];
                const float fraction = static_cast<float>(
                    std::min(1.0, static_cast<double>(n.rx.bytes) / twoGiB));
                const string label = formatByteSize(n.rx.bytes);
                ImGui::Text("%s", n.name.c_str());
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 16.0f), label.c_str());
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("TX"))
        {
            for (size_t i = 0; i < ifaces.size(); ++i)
            {
                const NetIfaceStats &n = ifaces[i];
                const float fraction = static_cast<float>(
                    std::min(1.0, static_cast<double>(n.tx.bytes) / twoGiB));
                const string label = formatByteSize(n.tx.bytes);
                ImGui::Text("%s", n.name.c_str());
                ImGui::ProgressBar(fraction, ImVec2(-1.0f, 16.0f), label.c_str());
            }
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

// Main code
int main(int, char **)
{
    // Setup SDL
    // (Some versions of SDL before <2.0.10 appears to have performance/stalling issues on a minority of Windows systems,
    // depending on whether SDL_INIT_GAMECONTROLLER is enabled or disabled.. updating to latest version of SDL is recommended!)
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER) != 0)
    {
        printf("Error: %s\n", SDL_GetError());
        return -1;
    }

    // GL 3.0 + GLSL 130
    const char *glsl_version = "#version 130";
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

    // Create window with graphics context
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_WindowFlags window_flags = (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Window *window = SDL_CreateWindow("Dear ImGui SDL2+OpenGL3 example", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, window_flags);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);
    SDL_GL_MakeCurrent(window, gl_context);
    SDL_GL_SetSwapInterval(1); // Enable vsync

    // Initialize OpenGL loader
#if defined(IMGUI_IMPL_OPENGL_LOADER_GL3W)
    bool err = gl3wInit() != 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLEW)
    bool err = glewInit() != GLEW_OK;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD)
    bool err = gladLoadGL() == 0;
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLAD2)
    bool err = gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress) == 0; // glad2 recommend using the windowing library loader instead of the (optionally) bundled one.
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING2)
    bool err = false;
    glbinding::Binding::initialize();
#elif defined(IMGUI_IMPL_OPENGL_LOADER_GLBINDING3)
    bool err = false;
    glbinding::initialize([](const char *name) { return (glbinding::ProcAddress)SDL_GL_GetProcAddress(name); });
#else
    bool err = false; // If you use IMGUI_IMPL_OPENGL_LOADER_CUSTOM, your loader is likely to requires some form of initialization.
#endif
    if (err)
    {
        fprintf(stderr, "Failed to initialize OpenGL loader!\n");
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // render bindings
    ImGuiIO &io = ImGui::GetIO();

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // background color
    // note : you are free to change the style of the application
    ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);

    // Main loop
    bool done = false;
    while (!done)
    {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
            if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame(window);
        ImGui::NewFrame();

        {
            ImVec2 mainDisplay = io.DisplaySize;
            memoryProcessesWindow("== Memory and Processes ==",
                                  ImVec2((mainDisplay.x / 2) - 20, (mainDisplay.y / 2) + 30),
                                  ImVec2((mainDisplay.x / 2) + 10, 10));
            // --------------------------------------
            systemWindow("== System ==",
                         ImVec2((mainDisplay.x / 2) - 10, (mainDisplay.y / 2) + 30),
                         ImVec2(10, 10));
            // --------------------------------------
            networkWindow("== Network ==",
                          ImVec2(mainDisplay.x - 20, (mainDisplay.y / 2) - 60),
                          ImVec2(10, (mainDisplay.y / 2) + 50));
        }

        // Rendering
        ImGui::Render();
        glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
