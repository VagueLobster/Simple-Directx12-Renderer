#include "CrossWindow/CrossWindow.h"
#include "Nutcrackz/Renderer/Renderer.h"

void xmain(int argc, const char** argv)
{
    // 🖼️ Create a window
    xwin::EventQueue eventQueue;
    xwin::Window window;

    xwin::WindowDesc windowDesc;
    windowDesc.name = "MainWindow";
    windowDesc.title = "Hello Triangle";
    windowDesc.visible = true;
    windowDesc.width = 1280;
    windowDesc.height = 720;
    //windowDesc.fullscreen = true;
    window.create(windowDesc, eventQueue);

    // 📸 Create a renderer
    Renderer renderer(window);

    // 🏁 Engine loop
    bool isRunning = true;
    while (isRunning)
    {
        // ♻️ Update the event queue
        eventQueue.update();

        // 🎈 Iterate through that queue:
        while (!eventQueue.empty())
        {
            //Update Events
            const xwin::Event& event = eventQueue.front();

            if (event.type == xwin::EventType::Resize)
            {
                const xwin::ResizeData data = event.data.resize;

                if (data.width > 0 && data.height > 0)
                    renderer.Resize(data.width, data.height);
            }

            if (event.type == xwin::EventType::Close)
            {
                window.close();
                isRunning = false;
            }

            eventQueue.pop();
        }

        // ✨ Update Visuals
        renderer.Render();
    }

}