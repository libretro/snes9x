/*****************************************************************************\
     Snes9x - Portable Super Nintendo Entertainment System (TM) emulator.
                This file is licensed under the Snes9x License.
   For further information, consult the LICENSE file in the root directory.
\*****************************************************************************/

#include "gtk_compat.h"
#include "gtk_display.h"
#include "gtk_display_driver_vulkan.h"
#include "gtk_shader_parameters.h"
#include "snes9x.h"
#include "gfx.h"
#include "fmt/format.h"

S9xVulkanDisplayDriver::S9xVulkanDisplayDriver(Snes9xWindow *_window, Snes9xConfig *_config)
{
    window = _window;
    config = _config;
    drawing_area = window->drawing_area;
    gdk_window = nullptr;
    gdk_display = nullptr;
    context.reset();
}

S9xVulkanDisplayDriver::~S9xVulkanDisplayDriver()
{
}

void S9xVulkanDisplayDriver::refresh()
{
    if (!context)
        return;

    context->swapchain->set_vsync(gui_config->sync_to_vblank);
    int new_width, new_height;

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_WINDOW(drawing_area->get_window()->gobj()))
    {
        wayland_surface->resize();
        std::tie(new_width, new_height) = wayland_surface->get_size();
    }
    else
#endif
    {
        new_width = drawing_area->get_width() * drawing_area->get_scale_factor();
        new_height = drawing_area->get_height() * drawing_area->get_scale_factor();
    }

    if (new_width != current_width || new_height != current_height)
    {
        context->recreate_swapchain(new_width, new_height);
        context->wait_idle();
        current_width = new_width;
        current_height = new_height;
    }
}

int S9xVulkanDisplayDriver::init()
{
    current_width = drawing_area->get_width() * drawing_area->get_scale_factor();
    current_height = drawing_area->get_height() * drawing_area->get_scale_factor();

    context = std::make_unique<Vulkan::Context>();

#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_WINDOW(drawing_area->get_window()->gobj()))
    {
        wayland_surface = std::make_unique<WaylandSurface>();
        if (!wayland_surface->attach(GTK_WIDGET(drawing_area->gobj())))
        {
            return -1;
        }
        context->init_wayland(wayland_surface->display, wayland_surface->child, current_width, current_height);
    }
#endif
    if (GDK_IS_X11_WINDOW(drawing_area->get_window()->gobj()))
    {
        display = gdk_x11_display_get_xdisplay(drawing_area->get_display()->gobj());
        xid = gdk_x11_window_get_xid(drawing_area->get_window()->gobj());

        context->init_Xlib(display, xid);
    }

    device = context->device;

    if (!gui_config->shader_filename.empty() && gui_config->use_shaders)
    {
        shaderchain = std::make_unique<Vulkan::ShaderChain>(context.get());
        setlocale(LC_NUMERIC, "C");
        if (!shaderchain->load_shader_preset(gui_config->shader_filename))
        {
            fmt::print("Couldn't load shader preset file\n");
            shaderchain = nullptr;
            setlocale(LC_NUMERIC, "");
        }
        else
        {
            window->enable_widget("shader_parameters_item", true);
            setlocale(LC_NUMERIC, "");
            return 0;
        }
    }

    simple_output = std::make_unique<Vulkan::SimpleOutput>(context.get(), vk::Format::eR5G6B5UnormPack16);

    return 0;
}

void S9xVulkanDisplayDriver::deinit()
{
    if (!context)
        return;

    if (shaderchain)
        gtk_shader_parameters_dialog_close();

    context->wait_idle();
}

void S9xVulkanDisplayDriver::update(uint16_t *buffer, int width, int height, int stride_in_pixels)
{
    if (!context)
        return;

    auto viewport = S9xApplyAspect(width, height, current_width, current_height);
    bool retval = false;

    if (shaderchain)
    {
        retval = shaderchain->do_frame_without_swap((uint8_t *)buffer, width, height, stride_in_pixels << 1, vk::Format::eR5G6B5UnormPack16, viewport.x, viewport.y, viewport.w, viewport.h);
    }
    else if (simple_output)
    {
        simple_output->set_filter(Settings.BilinearFilter);
        retval = simple_output->do_frame_without_swap((uint8_t *)buffer, width, height, stride_in_pixels << 1, viewport.x, viewport.y, viewport.w, viewport.h);
    }

    if (retval)
    {
        if (Settings.SkipFrames == THROTTLE_TIMER || Settings.SkipFrames == THROTTLE_TIMER_FRAMESKIP)
        {
            throttle.set_frame_rate(Settings.PAL ? PAL_PROGRESSIVE_FRAME_RATE : NTSC_PROGRESSIVE_FRAME_RATE);
            throttle.wait_for_frame_and_rebase_time();
        }

        context->swapchain->swap();

        if (gui_config->reduce_input_lag)
            context->wait_idle();
    }
}

int S9xVulkanDisplayDriver::query_availability()
{
    return 0;
}

void *S9xVulkanDisplayDriver::get_parameters()
{
    if (shaderchain)
        return &shaderchain->preset->parameters;
    return nullptr;
}

void S9xVulkanDisplayDriver::save(const char *filename)
{
    setlocale(LC_NUMERIC, "C");
    if (shaderchain)
        shaderchain->preset->save_to_file(filename);
    setlocale(LC_NUMERIC, "");
}

bool S9xVulkanDisplayDriver::is_ready()
{
    return true;
}