// ============================================================
// SlormZoom for The Slormancer (Aurie + YYToolkit)
//
// Lets the camera zoom out further than the game allows (75% in the game's Zoom setting).
// The game keeps its zoom limit in a variable named max_zoom: we raise it, live, a few times
// per second, so the mouse wheel and the game's Zoom setting can go further.
//
// Settings (saved in SlormZoom.ini, next to the DLL):
//   - before launch: by editing SlormZoom.ini
//   - in game, mouse: "M" button in the game's Options menu (if ModMenu.dll is installed)
// ============================================================

#include <YYToolkit/YYTK_Shared.hpp>
#include <ModMenu/ModMenuAPI.hpp>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace Aurie;
using namespace YYTK;

// ============================================================
// Constants
// ============================================================

// The game's own zoom limit. Its Zoom setting shows (2 - limit) * 100 %: 1.25 = 75%.
static constexpr double kVanillaMaxZoom = 1.25;

// Limits offered in game, from the game's own one to the furthest (75%, 50%, 25%, 10%, 0%).
static constexpr double kZoomPresets[] = { 1.25, 1.5, 1.75, 1.9, 2.0 };
static constexpr int kZoomPresetCount = 5;

// Accepted range in SlormZoom.ini (above 2.0 the game's label goes negative, which is cosmetic).
static constexpr double kMinMaxZoom = 0.75;
static constexpr double kMaxMaxZoom = 4.0;

// The limit is applied every N End Steps (about 4 times per second at 120 fps).
static constexpr int APPLY_INTERVAL = 30;

// While the variable has not been found, look for it again every N End Steps.
static constexpr int SEARCH_INTERVAL = 240;

// ============================================================
// Settings (loaded from SlormZoom.ini, editable in game)
// ============================================================

static bool   g_Enabled = true;    // zoom-out enabled
static double g_MaxZoom = 1.9;     // furthest zoom allowed (game label 10%)
static bool   g_Debug = false;     // detailed logs

static char g_ConfigPath[MAX_PATH] = {};

// ============================================================
// Globals
// ============================================================

static YYTKInterface* g_Yytk = nullptr;

// Where the game keeps max_zoom: a global variable, or a variable of one instance (a camera).
enum HolderKind { HOLDER_NONE, HOLDER_GLOBAL, HOLDER_INSTANCE };
static HolderKind g_Holder = HOLDER_NONE;
static RValue g_HolderInst;
static bool g_ForceApply = true;      // apply on the next End Step (setting just changed)

// ============================================================
// GameMaker helpers
// ============================================================

// Converts a numeric GameMaker value to a double (through its text form).
static double ReadNumber(RValue Value)
{
    std::string s(Value.ToString());
    return std::atof(s.c_str());
}

// True if the GameMaker value is truthy (true, 1, or any non-zero number).
static bool IsTrue(RValue Value)
{
    std::string s(Value.ToString());
    if (s == "true" || s == "True") return true;
    return std::atof(s.c_str()) != 0.0;
}

// Game label for a zoom limit, like the game's Zoom setting: (2 - limit) * 100 %.
static void DescribeZoom(double MaxZoom, char* Out, size_t Size)
{
    std::snprintf(Out, Size, "%.0f%%", (2.0 - MaxZoom) * 100.0);
}

// ============================================================
// Configuration (SlormZoom.ini)
// ============================================================

static void ConfigWrite(const char* Key, const char* Value)
{
    WritePrivateProfileStringA("SlormZoom", Key, Value, g_ConfigPath);
}

static void ConfigCreateDefaultFile()
{
    FILE* f = nullptr;
    if (fopen_s(&f, g_ConfigPath, "w") != 0 || !f)
        return;

    std::fputs(
        "; SlormZoom configuration for The Slormancer\n"
        ";\n"
        "; Enabled : 1 = zoom further out than the game allows, 0 = the game's own limit\n"
        "; MaxZoom : furthest zoom-out. The game's Zoom setting shows (2 - MaxZoom) * 100 %:\n"
        ";           1.25 = 75% (the game's own)   1.5 = 50%   1.75 = 25%   1.9 = 10%   2.0 = 0%\n"
        ";           Range 0.75 to 4. Above 2.0 the label goes negative, which is only cosmetic.\n"
        "; Debug   : 1 = detailed logs in the console\n"
        ";\n"
        "; In game: M button in the game's Options menu (needs ModMenu.dll)\n"
        "\n"
        "[SlormZoom]\n"
        "Enabled=1\n"
        "MaxZoom=1.9\n"
        "Debug=0\n",
        f);

    std::fclose(f);
}

static void ConfigLoad()
{
    if (GetFileAttributesA(g_ConfigPath) == INVALID_FILE_ATTRIBUTES)
        ConfigCreateDefaultFile();

    g_Enabled = GetPrivateProfileIntA("SlormZoom", "Enabled", 1, g_ConfigPath) != 0;
    g_Debug = GetPrivateProfileIntA("SlormZoom", "Debug", 0, g_ConfigPath) != 0;

    char value[32] = {};
    GetPrivateProfileStringA("SlormZoom", "MaxZoom", "1.9", value, sizeof(value), g_ConfigPath);
    g_MaxZoom = std::atof(value);

    if (!(g_MaxZoom > kMinMaxZoom && g_MaxZoom <= kMaxMaxZoom))
    {
        DbgPrintEx(LOG_SEVERITY_WARNING, "[SlormZoom] MaxZoom=%s out of range (0.75, 4]: using 1.9", value);
        g_MaxZoom = 1.9;
    }
}

// ============================================================
// In-game settings: zoom limit, toggle
// ============================================================

static void ZoomStep(int Direction)
{
    // Index of the closest preset, then one step further or closer.
    int index = 0;
    for (int i = 1; i < kZoomPresetCount; ++i)
    {
        if (std::fabs(kZoomPresets[i] - g_MaxZoom) < std::fabs(kZoomPresets[index] - g_MaxZoom))
            index = i;
    }

    index += Direction;
    if (index < 0) index = 0;
    if (index >= kZoomPresetCount) index = kZoomPresetCount - 1;

    g_MaxZoom = kZoomPresets[index];
    g_ForceApply = true;

    char value[16];
    std::snprintf(value, sizeof(value), "%.2f", g_MaxZoom);
    ConfigWrite("MaxZoom", value);

    char label[16];
    DescribeZoom(g_MaxZoom, label, sizeof(label));
    DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] Furthest zoom: %s (max_zoom %.2f)", label, g_MaxZoom);
}

static void ToggleEnabled()
{
    g_Enabled = !g_Enabled;
    g_ForceApply = true;
    ConfigWrite("Enabled", g_Enabled ? "1" : "0");
    DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] Zoom-out: %s", g_Enabled ? "ON" : "OFF");
}

// ============================================================
// The game's max_zoom variable: found once, then raised a few times per second.
// ============================================================

// Logs a variable of the holder, if it exists (Debug=1).
static void LogHolderVariable(const char* Name)
{
    RValue value;
    if (g_Holder == HOLDER_GLOBAL)
    {
        if (!IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { Name })))
            return;
        value = g_Yytk->CallBuiltin("variable_global_get", { Name });
    }
    else
    {
        if (!IsTrue(g_Yytk->CallBuiltin("variable_instance_exists", { g_HolderInst, Name })))
            return;
        value = g_Yytk->CallBuiltin("variable_instance_get", { g_HolderInst, Name });
    }

    std::string text(value.ToString());
    DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom]   %s = %s", Name, text.c_str());
}

// Looks for max_zoom: first as a global variable, then on every instance of the room.
static bool FindHolder()
{
    if (IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "max_zoom" })))
    {
        g_Holder = HOLDER_GLOBAL;
        DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] max_zoom found: global variable");
    }
    else
    {
        RValue all(-3.0);   // GameMaker "all" constant
        const int n = static_cast<int>(ReadNumber(g_Yytk->CallBuiltin("instance_number", { all })));

        for (int i = 0; i < n && g_Holder == HOLDER_NONE; ++i)
        {
            RValue inst = g_Yytk->CallBuiltin("instance_find", { all, RValue(static_cast<double>(i)) });
            if (!IsTrue(g_Yytk->CallBuiltin("variable_instance_exists", { inst, "max_zoom" })))
                continue;

            g_Holder = HOLDER_INSTANCE;
            g_HolderInst = inst;

            RValue objectIndex = g_Yytk->CallBuiltin("variable_instance_get", { inst, "object_index" });
            std::string objectName(g_Yytk->CallBuiltin("object_get_name", { objectIndex }).ToString());
            DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] max_zoom found: variable of %s", objectName.c_str());
        }
    }

    if (g_Holder == HOLDER_NONE)
        return false;

    if (g_Debug)
    {
        for (const char* name : { "max_zoom", "min_zoom", "zoom", "zoom_target", "zoom_speed", "zm" })
            LogHolderVariable(name);

        if (IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "zm" })))
        {
            std::string zm(g_Yytk->CallBuiltin("variable_global_get", { "zm" }).ToString());
            DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom]   global.zm = %s", zm.c_str());
        }
    }

    return true;
}

// True while the holder found earlier still exists (the camera can be recreated with the room).
static bool HolderStillValid()
{
    if (g_Holder == HOLDER_GLOBAL)
        return IsTrue(g_Yytk->CallBuiltin("variable_global_exists", { "max_zoom" }));

    if (g_Holder == HOLDER_INSTANCE)
        return IsTrue(g_Yytk->CallBuiltin("instance_exists", { g_HolderInst }))
            && IsTrue(g_Yytk->CallBuiltin("variable_instance_exists", { g_HolderInst, "max_zoom" }));

    return false;
}

static double ReadMaxZoom()
{
    if (g_Holder == HOLDER_GLOBAL)
        return ReadNumber(g_Yytk->CallBuiltin("variable_global_get", { "max_zoom" }));

    return ReadNumber(g_Yytk->CallBuiltin("variable_instance_get", { g_HolderInst, "max_zoom" }));
}

static void WriteMaxZoom(double Value)
{
    if (g_Holder == HOLDER_GLOBAL)
        g_Yytk->CallBuiltin("variable_global_set", { "max_zoom", RValue(Value) });
    else
        g_Yytk->CallBuiltin("variable_instance_set", { g_HolderInst, "max_zoom", RValue(Value) });
}

// Once per End Step.
static void OnEndStep()
{
    static int tick = 0;
    static int lastSearch = -SEARCH_INTERVAL;
    static bool reportedMissing = false;
    ++tick;

    if (!g_ForceApply && (tick % APPLY_INTERVAL) != 0)
        return;

    if (g_Holder != HOLDER_NONE && !HolderStillValid())
    {
        g_Holder = HOLDER_NONE;
        g_HolderInst = RValue();
    }

    if (g_Holder == HOLDER_NONE)
    {
        if (tick - lastSearch < SEARCH_INTERVAL)
            return;

        lastSearch = tick;
        if (!FindHolder())
        {
            if (g_Debug && !reportedMissing)
                DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] max_zoom not found yet (normal in the main menu)");
            reportedMissing = true;
            return;
        }
    }

    g_ForceApply = false;

    const double target = g_Enabled ? g_MaxZoom : kVanillaMaxZoom;
    const double current = ReadMaxZoom();

    if (std::fabs(current - target) > 0.0001)
    {
        WriteMaxZoom(target);
        if (g_Debug)
            DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] max_zoom %.2f -> %.2f", current, target);
    }
}

// Every object event goes through here: we only act after the controller's End Step.
static void CodeEventCallback(FWCodeEvent& Context)
{
    CCode* code = std::get<2>(Context.Arguments());
    if (!code || !code->GetName())
        return;

    if (std::strcmp(code->GetName(), "gml_Object_obj_controller_Step_2") != 0)
        return;

    if (!Context.CalledOriginal())
        Context.Call();

    OnEndStep();
}

// ============================================================
// Settings page in the mods menu ("M" button of the Options menu).
//
// The menu itself lives in ModMenu.dll: we register with it if it is installed. Without it,
// the zoom-out still works, with SlormZoom.ini.
// ============================================================

// The menu language comes from ModMenu (English by default, French as an option). GetLanguage
// and HelpLine only exist from interface version 1.1 on.
static bool g_MenuHasLanguage = false;
static bool g_MenuFrench = false;

// French accented letters as explicit UTF-8, so the texts do not depend on the source file
// encoding. Always concatenated ("D" FR_E_ACUTE "zoom") so a following letter is never read
// as part of the escape.
#define FR_E_ACUTE    "\xC3\xA9"
#define FR_E_ACUTE_UP "\xC3\x89"

// Picks the text for the menu's current language.
static const char* Tr(const char* English, const char* French)
{
    return g_MenuFrench ? French : English;
}

static bool MenuIsActive(void*)
{
    return g_Enabled;
}

static void MenuDrawPage(ModMenu::IModMenu* Menu, void*)
{
    g_MenuFrench = g_MenuHasLanguage && Menu->GetLanguage() == ModMenu::kLangFrench;

    if (Menu->Toggle(0, 0, Tr("Zoom-out enabled", "D" FR_E_ACUTE "zoom activ" FR_E_ACUTE), g_Enabled))
        ToggleEnabled();

    char label[16];
    DescribeZoom(g_MaxZoom, label, sizeof(label));

    const int step = Menu->Selector(0, 1, Tr("Furthest zoom", "Zoom le plus loin"), label);
    if (step != 0)
        ZoomStep(step);

    if (g_MenuHasLanguage)
        Menu->HelpLine(Tr(
            "Zoom with the mouse wheel or the game's Zoom setting  -  Esc: back",
            "Zoome avec la molette ou le r" FR_E_ACUTE "glage Zoom du jeu  -  " FR_E_ACUTE_UP "chap : retour"));
}

// Returns true if ModMenu.dll is installed and accepted our page.
static bool RegisterInModMenu()
{
    AurieInterfaceBase* base = nullptr;
    if (!AurieSuccess(ObGetInterface(ModMenu::kInterfaceName, base)) || !base)
        return false;

    ModMenu::ModEntry entry = {};
    entry.Name = "SLORMZOOM";
    entry.Title = "MOD  -  SLORMZOOM";
    entry.Help = "Zoom with the mouse wheel or the game's Zoom setting  -  Esc: back";
    entry.IsActive = MenuIsActive;
    entry.DrawPage = MenuDrawPage;

    short major = 0, minor = 0, patch = 0;
    base->QueryVersion(major, minor, patch);
    g_MenuHasLanguage = (major > 1) || (major == 1 && minor >= 1);

    return static_cast<ModMenu::IModMenu*>(base)->RegisterMod(entry);
}

// ============================================================
// Module initialization
// ============================================================

EXPORTED AurieStatus ModuleInitialize(
    IN AurieModule* Module,
    IN const fs::path& ModulePath
)
{
    g_Yytk = YYTK::GetInterface();

    if (!g_Yytk)
    {
        OutputDebugStringA("[SlormZoom] ERROR: YYToolkit interface not found\n");
        return AURIE_MODULE_DEPENDENCY_NOT_RESOLVED;
    }

    // --- Configuration file: SlormZoom.ini next to the DLL ---
    const std::string iniPath = (ModulePath.parent_path() / "SlormZoom.ini").string();
    strncpy_s(g_ConfigPath, iniPath.c_str(), _TRUNCATE);

    ConfigLoad();

    AurieStatus status = g_Yytk->CreateCallback(Module, EVENT_OBJECT_CALL, reinterpret_cast<PVOID>(CodeEventCallback), 0);
    if (!AurieSuccess(status))
    {
        DbgPrintEx(LOG_SEVERITY_ERROR, "[SlormZoom] could not register the event callback (%s)", AurieStatusToString(status));
        return status;
    }

    // --- Settings page in the mods menu: optional, the zoom-out works without it ---
    const bool okMenu = RegisterInModMenu();

    char label[16];
    DescribeZoom(g_MaxZoom, label, sizeof(label));

    DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] ready: zoom-out %s, furthest zoom %s (max_zoom %.2f)",
        g_Enabled ? "ON" : "OFF", label, g_MaxZoom);
    DbgPrintEx(LOG_SEVERITY_INFO, "[SlormZoom] mods menu (ModMenu.dll): %s",
        okMenu ? "settings page added" : "not installed (SlormZoom.ini only)");

    return AURIE_SUCCESS;
}
