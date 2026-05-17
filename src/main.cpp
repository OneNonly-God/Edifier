#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "icon.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#elif __linux__
#include <gtk/gtk.h>
#include <sys/stat.h>
#endif

namespace fs = std::filesystem;

enum ThemeType { THEME_DARK, THEME_LIGHT, THEME_GREY, THEME_CUSTOM };
ThemeType currentTheme = THEME_GREY;

static GLFWwindow* g_window = nullptr;
static bool showThemeEditor = false;
static ImVec4 originalThemeColors[ImGuiCol_COUNT];
static bool themeBackupSaved = false;

// GTK is initialized once at startup, not per-dialog.
#ifdef __linux__
static bool g_gtkInitialized = false;
#endif

// A single open file / tab representation
struct FileTab {
    std::string filePath;
    std::string content;
    std::vector<char> editBuffer;
    bool isModified = false;
    std::filesystem::file_time_type lastModified;
    bool isReadonly = false;
    int cachedWordCount = 0;
    size_t cachedCharCount = 0;
};

struct AppState {
    std::vector<FileTab> tabs;
    int activeTab = -1;
    int closeTabIndex = -1;
    int lastActiveTab = -1;  // Track previous tab to detect changes

    // UI & dialog flags
    bool needsSave = false;
    bool showAboutDialog = false;
    bool showFileDialog = false;
    bool firstRun = true;

    // File browser / UI helpers
    std::string currentPath;
    char filePathBuffer[512] = "";

    // UI focus
    bool focusEditor = false;

    std::string projectRoot;
};

AppState g_appState;

// Forward declarations
void RenderExplorer();
void RenderFileSystemTree(const fs::path& path);
void OpenFolder(const std::string& folderpath);
void SetupInitialStyle();
void ThemeEditorMenu();
void HandleKeyboardShortcuts();
void SetupInitialDockingLayout();
std::string OpenFileDialog();
void OpenFile(const std::string& filepath);
void SaveFileAs(int tabIndex);
void SaveFile(int tabIndex);
void CloseTab(int tabIndex);
void RenderEditor();
void SaveAll();
void RenderMenuBar();
void RenderMainDockSpace();
bool IsTextFile(const std::string& filepath);
std::string ReadFileContent(const std::string& filepath);



// Embedded Icon
void setEmbeddedIcon(GLFWwindow* window) {
    GLFWimage icon;
    icon.pixels = stbi_load_from_memory(icon_png, icon_png_len, &icon.width, &icon.height, nullptr, 4);
    if (!icon.pixels) {
        std::cerr << "Failed to load embedded icon." << std::endl;
        return;
    }

    glfwSetWindowIcon(window, 1, &icon);
    stbi_image_free(icon.pixels);
}

// Clean up GTK resources
void gtkCleanup() {
    #ifdef __linux__
    while (gtk_events_pending()) {
        gtk_main_iteration();
    }
    #endif
}

void gtkInit() {
#ifdef __linux__
    if (!g_gtkInitialized) {
        g_gtkInitialized = gtk_init_check(NULL, NULL);
        if (!g_gtkInitialized) {
            std::cerr << "GTK init failed\n";
        }
    }
#endif
}

void OpenFolder(const std::string& folderpath) {
    if (folderpath.empty()) return;
    
    if (!fs::exists(folderpath) || !fs::is_directory(folderpath)) {
        std::cerr << "Invalid folder path: " << folderpath << "\n";
        return;
    }

    g_appState.projectRoot = folderpath;
    g_appState.currentPath = folderpath;
}

std::string OpenFileDialog() {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "All Files\0*.*\0Text Files\0*.txt\0C++ Files\0*.cpp;*.h;*.hpp\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Open File";
    ofn.Flags = OFN_DONTADDTORECENT | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn)) {
        return std::string(filename);
    }
#elif __linux__
    if (!g_gtkInitialized) return "";

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open File",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_OPEN,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Open", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = std::string(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    gtkCleanup();

    return result;
#else
    g_appState.showFileDialog = true;
    return "";
#endif
    return "";
}

std::string SaveFileDialog(const std::string& defaultName = "") {
#ifdef _WIN32
    char filename[MAX_PATH] = "";
    if (!defaultName.empty()) {
        strncpy(filename, defaultName.c_str(), MAX_PATH - 1);
        filename[MAX_PATH - 1] = '\0';
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = "Text Files\0*.txt\0All Files\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = "Save File As";
    ofn.Flags = OFN_OVERWRITEPROMPT;
    ofn.lpstrDefExt = "txt";

    if (GetSaveFileNameA(&ofn)) {
        return std::string(filename);
    }
#elif __linux__
    if (!g_gtkInitialized) return "";

    GtkWidget *dialog = gtk_file_chooser_dialog_new("Save File As",
                                                    NULL,
                                                    GTK_FILE_CHOOSER_ACTION_SAVE,
                                                    "_Cancel", GTK_RESPONSE_CANCEL,
                                                    "_Save", GTK_RESPONSE_ACCEPT,
                                                    NULL);

    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog), TRUE);

    if (!defaultName.empty()) {
        gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog), defaultName.c_str());
    }

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = std::string(filename);
        g_free(filename);
    }

    gtk_widget_destroy(dialog);
    gtkCleanup();

    return result;
#else
    g_appState.showFileDialog = true;
    return "";
#endif
    return "";
}

std::string OpenFolderDialog() {
#ifdef _WIN32
    std::string result = "";
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    if (FAILED(hr)) {
        std::cerr << "CoInitializeEx failed\n";
        return "";
    }
    
    IFileOpenDialog* pFileOpen = nullptr;
    hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, 
                          IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));

    if (SUCCEEDED(hr) && pFileOpen != nullptr) {
        DWORD dwOptions;
        if (SUCCEEDED(pFileOpen->GetOptions(&dwOptions))) {
            pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        }

        hr = pFileOpen->Show(NULL);

        if (SUCCEEDED(hr)) {
            IShellItem* pItem = nullptr;
            hr = pFileOpen->GetResult(&pItem);
            if (SUCCEEDED(hr) && pItem != nullptr) {
                PWSTR pszFilePath = nullptr;
                hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

                if (SUCCEEDED(hr) && pszFilePath != nullptr) {
                    int size_needed = WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, NULL, 0, NULL, NULL);
                    std::string strTo(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, pszFilePath, -1, &strTo[0], size_needed, NULL, NULL);
                    
                    if (!strTo.empty() && strTo.back() == '\0') strTo.pop_back();
                    
                    result = strTo;
                    CoTaskMemFree(pszFilePath);
                }
                pItem->Release();
            }
        }
        pFileOpen->Release();
    } else {
        std::cerr << "Failed to create FileOpenDialog\n";
    }
    CoUninitialize();
    return result;
    
#elif __linux__
    if (!g_gtkInitialized) return "";
    GtkWidget *dialog = gtk_file_chooser_dialog_new("Open Folder", NULL, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER, "_Cancel", GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        result = std::string(filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
    gtkCleanup();
    return result;
#else
    g_appState.showFileDialog = true;
    return "";
#endif
    return "";
}

void RenderFileSystemTree(const fs::path& path) {

    if (path.empty() || !fs::exists(path) || !fs::is_directory(path)) {
        return;
    }
    std::vector<fs::directory_entry> directories;
    std::vector<fs::directory_entry> files;

    try {
        for (const auto& entry : fs::directory_iterator(path)) {
            // Hidden file filtering is Linux-only (leading dot convention).
            // On Windows, hidden status is a filesystem attribute, not a naming convention.
#ifdef __linux__
            if (entry.path().filename().string().rfind(".", 0) == 0) continue;
#endif

            try {
                if (entry.is_directory()) {
                    directories.push_back(entry);
                } else {
                    files.push_back(entry);
                }
            } catch (const fs::filesystem_error& e) {
                std::cerr << "Error accessing entry: " << e.what() << "\n";
                continue;
            }
        }
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Error reading directory " << path << ": " << e.what() << "\n";
        return;
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error in file tree: " << e.what() << "\n";
        return;
    }

    auto sortFunc = [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    };
    std::sort(directories.begin(), directories.end(), sortFunc);
    std::sort(files.begin(), files.end(), sortFunc);

    for (const auto& dir : directories) {
        std::string dirname = dir.path().filename().string();
        std::string pathStr = dir.path().string();

        ImGui::PushID(pathStr.c_str()); 
        
        bool nodeOpen = ImGui::TreeNodeEx(dirname.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
        
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Set as Root")) {
                OpenFolder(pathStr);
            }
            ImGui::EndPopup();
        }

        if (nodeOpen) {
            RenderFileSystemTree(dir.path());
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    for (const auto& file : files) {
        std::string filename = file.path().filename().string();
        std::string pathStr = file.path().string();

        bool isSelected = false;
        if (g_appState.activeTab >= 0 && g_appState.activeTab < (int)g_appState.tabs.size()) {
            if (g_appState.tabs[g_appState.activeTab].filePath == pathStr) {
                isSelected = true;
            }
        }

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected) flags |= ImGuiTreeNodeFlags_Selected;

        ImGui::PushID(pathStr.c_str());
        
        ImGui::TreeNodeEx(filename.c_str(), flags);
        if (ImGui::IsItemClicked() || ImGui::IsItemActivated()) {
            OpenFile(pathStr);
        }
        
        ImGui::PopID();
    }
}

void RenderExplorer() {
    ImGui::Begin("Explorer");

    if (g_appState.projectRoot.empty()) {
        ImVec2 size = ImGui::GetContentRegionAvail();
        ImGui::SetCursorPos(ImVec2(size.x * 0.1f, size.y * 0.4f));
        
        if (ImGui::Button("Open Folder", ImVec2(size.x * 0.8f, 0))) {
            std::string folder = OpenFolderDialog();
            if (!folder.empty()) {
                g_appState.projectRoot = folder;
            }
        }
    } else {
        std::string rootName = fs::path(g_appState.projectRoot).filename().string();
        ImGui::TextColored(ImVec4(0.6f, 0.4f, 0.8f, 1.0f), "%s", rootName.c_str());
        
        ImGui::SameLine(ImGui::GetWindowWidth() - 30);
        if (ImGui::SmallButton("X")) {
            g_appState.projectRoot.clear();
            g_appState.currentPath.clear();
        }
        
        ImGui::Separator();
        
        ImGui::BeginChild("FileTree");
        RenderFileSystemTree(g_appState.projectRoot);
        ImGui::EndChild();
    }

    ImGui::End();
}

bool IsTextFile(const std::string& filepath) {
    if (filepath.empty() || !fs::exists(filepath)) return false;
    
    std::string ext = fs::path(filepath).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });

    std::vector<std::string> textExtensions = {
        ".txt", ".md", ".markdown", ".log", ".cfg", ".ini", ".json", ".xml",
        ".html", ".htm", ".css", ".js", ".ts", ".jsx", ".tsx",
        ".cpp", ".c", ".h", ".hpp", ".cc", ".cxx", ".py", ".java",
        ".cs", ".rb", ".go", ".rs", ".swift", ".kt", ".scala",
        ".sh", ".bash", ".zsh", ".fish", ".ps1", ".bat", ".cmd",
        ".yaml", ".yml", ".toml", ".env", ".gitignore", ".dockerignore"
    };

    if (std::find(textExtensions.begin(), textExtensions.end(), ext) != textExtensions.end()) {
        return true;
    }

    if (ext.empty()) return true;

    // Binary check via sampling
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    char buffer[512];
    file.read(buffer, sizeof(buffer));
    std::streamsize bytesRead = file.gcount();
    file.close();

    for (std::streamsize i = 0; i < bytesRead; ++i) {
        unsigned char c = static_cast<unsigned char>(buffer[i]);
        if (c < 32 && c != 9 && c != 10 && c != 13) {
            if (c == 0) return false;
        }
    }

    return true;
}

std::string ReadFileContent(const std::string& filepath) {
    if (filepath.empty() || !fs::exists(filepath)) {
        std::cerr << "File does not exist: " << filepath << "\n";
        return "";
    }
    
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filepath << "\n";
        return "";
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    const size_t maxSize = 10 * 1024 * 1024; // 10MB
    if (size > (std::streamsize)maxSize) {
        std::string content(maxSize, '\0');
        file.read(&content[0], maxSize);
        content += "\n\n[File truncated - original size: " + std::to_string(size) + " bytes]";
        return content;
    }

    std::string content((size_t)size, '\0');
    if(!content.empty()) { 
        file.read(content.data(), size);
    }

    std::string normalized;
    normalized.reserve(content.size());
    for (size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\r') {
            if (i + 1 < content.size() && content[i + 1] == '\n') {
                normalized += '\n';
                ++i;
            } else {
                normalized += '\n';
            }
        } else {
            normalized += content[i];
        }
    }

    return normalized;
}



// Helper to update word/char statistics for a tab.
void UpdateFileStats(FileTab& tab) {
    tab.cachedCharCount = tab.content.length();
    int wc = 0;
    bool inword = false;
    for(char c : tab.content) {
        if(std::isspace(static_cast<unsigned char>(c))) {
            inword = false;
        } else if(!inword) {
            inword = true;
            ++wc;
        }
    }
    tab.cachedWordCount = wc;
}

// Sync the live edit buffer back to tab.content before any save operation.
// HandleKeyboardShortcuts runs before RenderEditor each frame, so without this the
// most recent frame's keystrokes would not yet be reflected in tab.content.
void SyncTabContent(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) return;
    FileTab& tab = g_appState.tabs[tabIndex];
    if (!tab.editBuffer.empty()) {
        std::string synced(tab.editBuffer.data());
        if (synced != tab.content) {
            tab.content = synced;
            UpdateFileStats(tab);
        }
    }
}

void SaveFile(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) {
        std::cerr << "Invalid tab index: " << tabIndex << "\n";
        return;
    }

    // Flush editBuffer → content before writing to disk.
    SyncTabContent(tabIndex);

    FileTab &tab = g_appState.tabs[tabIndex];

    if (tab.filePath.empty()) {
        SaveFileAs(tabIndex);
        return;
    }

    std::ofstream file(tab.filePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to save file: " << tab.filePath << "\n";
        return;
    }

    file << tab.content;
    file.close();

    tab.isModified = false;
    g_appState.needsSave = false;

    for (const auto& t : g_appState.tabs) {
        if (t.isModified) {
            g_appState.needsSave = true;
            break;
        }
    }

    if (fs::exists(tab.filePath)) {
        tab.lastModified = fs::last_write_time(tab.filePath);
    }
    
    std::cout << "Saved: " << tab.filePath << "\n";
}

void SaveFileAs(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) return;

    // Flush editBuffer → content before writing to disk.
    SyncTabContent(tabIndex);

    std::string defaultName = "untitled.txt";
    if (!g_appState.tabs[tabIndex].filePath.empty()) {
        defaultName = fs::path(g_appState.tabs[tabIndex].filePath).filename().string();
    }

    std::string filepath = SaveFileDialog(defaultName);
    if (filepath.empty()) return;

    std::ofstream file(filepath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        std::cerr << "Failed to Save As: " << filepath << "\n";
        return;
    }

    file << g_appState.tabs[tabIndex].content;
    file.close();

    g_appState.tabs[tabIndex].filePath = filepath;
    g_appState.tabs[tabIndex].isModified = false;
    g_appState.needsSave = false;

    for (const auto& t : g_appState.tabs) {
        if (t.isModified) {
            g_appState.needsSave = true;
            break;
        }
    }
    
    if (fs::exists(filepath)) {
        g_appState.tabs[tabIndex].lastModified = fs::last_write_time(filepath);
    }
    
    std::cout << "Saved As: " << filepath << "\n";
}

void SaveAll() {
    for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
        if (g_appState.tabs[i].isModified) {
            SaveFile(i);
        }
    }
}

void OpenFile(const std::string& filepath) {
    if (filepath.empty()) return;
    
    if (!fs::exists(filepath)) {
        std::cerr << "File does not exist: " << filepath << "\n";
        return;
    }

    // Check if file is already open
    for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
        if (g_appState.tabs[i].filePath == filepath) {
            g_appState.activeTab = i;
            g_appState.focusEditor = true;
            return;
        }
    }

    FileTab tab;
    tab.filePath = filepath;

    // call IsTextFile before reading content.
    // Previously IsTextFile was defined but never invoked here, so binary files
    // were opened and their raw bytes were dumped into the ImGui text buffer.
    if (IsTextFile(filepath)) {
        tab.content = ReadFileContent(filepath);
    } else {
        tab.content = "[Binary file: " + filepath + "]\n"
                      "[Size: " + std::to_string(fs::file_size(filepath)) + " bytes]\n\n"
                      "This file appears to be binary and cannot be displayed as text.";
    }

    tab.lastModified = fs::last_write_time(filepath);
    tab.isReadonly = false;
    tab.isModified = false;
    UpdateFileStats(tab);

    g_appState.tabs.push_back(std::move(tab));
    g_appState.activeTab = (int)g_appState.tabs.size() - 1;

    g_appState.focusEditor = true;
}

void CloseTab(int tabIndex) {
    if (tabIndex < 0 || tabIndex >= (int)g_appState.tabs.size()) return;

    g_appState.tabs.erase(g_appState.tabs.begin() + tabIndex);
    
    if (g_appState.tabs.empty()) {
        g_appState.activeTab = -1;
        g_appState.needsSave = false;
    } else {
        // Correctly update activeTab after erasure.
        // Erasing a tab shifts all higher indices down by one.
        // If the closed tab was below activeTab, decrement activeTab to track the same tab.
        // If the closed tab was activeTab, clamp to the new end of the list.
        // If the closed tab was above activeTab, no change is needed.
        if (tabIndex < g_appState.activeTab) {
            --g_appState.activeTab;
        } else if (tabIndex == g_appState.activeTab) {
            g_appState.activeTab = std::min(tabIndex, (int)g_appState.tabs.size() - 1);
        }
        // tabIndex > g_appState.activeTab: activeTab is unaffected.
        
        g_appState.needsSave = false;
        for (const auto& tab : g_appState.tabs) {
            if (tab.isModified) {
                g_appState.needsSave = true;
                break;
            }
        }
    }
}

void SetupInitialStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 10.0f;
    style.FrameRounding = 10.0f;
    style.ScrollbarRounding = 10.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;

    style.FramePadding = ImVec2(8, 4);
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 14.0f;
    style.GrabMinSize = 12.0f;

    ImVec4* colors = style.Colors;

    const ImVec4 bg         = ImVec4(0.04f, 0.03f, 0.06f, 1.00f);
    const ImVec4 panel      = ImVec4(0.05f, 0.04f, 0.05f, 1.00f);
    const ImVec4 panelAlt   = ImVec4(0.11f, 0.11f, 0.12f, 1.00f);
    const ImVec4 accent     = ImVec4(0.15f, 0.14f, 0.15f, 1.00f);
    const ImVec4 accentHov  = ImVec4(0.38f, 0.38f, 0.39f, 1.00f);
    const ImVec4 accentAct  = ImVec4(0.46f, 0.45f, 0.46f, 1.00f);
    const ImVec4 muted      = ImVec4(0.44f, 0.40f, 0.48f, 1.00f);
    const ImVec4 borderCol  = ImVec4(0.13f, 0.13f, 0.13f, 0.65f);

    colors[ImGuiCol_Text]                 = ImVec4(0.96f, 0.94f, 0.99f, 1.00f);
    colors[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.42f, 0.50f, 1.00f);
    colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.54f, 0.54f, 0.54f, 0.90f);
    colors[ImGuiCol_WindowBg]             = bg;
    colors[ImGuiCol_ChildBg]              = panel;
    colors[ImGuiCol_PopupBg]              = ImVec4(0.08f, 0.07f, 0.07f, 0.95f);
    colors[ImGuiCol_Border]               = borderCol;
    colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]              = panelAlt;
    colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.24f, 0.24f, 0.25f, 1.00f);
    colors[ImGuiCol_FrameBgActive]        = ImVec4(0.44f, 0.44f, 0.44f, 1.00f);
    colors[ImGuiCol_TitleBg]              = ImVec4(0.12f, 0.11f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive]        = ImVec4(0.13f, 0.12f, 0.14f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.05f, 0.03f, 0.06f, 0.75f);
    colors[ImGuiCol_MenuBarBg]            = ImVec4(0.13f, 0.12f, 0.13f, 1.00f);
    colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.07f, 0.06f, 0.07f, 1.00f);
    colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.25f, 0.24f, 0.25f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.31f, 0.30f, 0.32f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.37f, 0.39f, 1.00f);
    colors[ImGuiCol_TabDimmedSelected]    = ImVec4(0.15f, 0.16f, 0.18f, 1.00f);
    colors[ImGuiCol_TabSelected]          = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_Tab]                  = ImVec4(0.16f, 0.17f, 0.18f, 1.00f);
    colors[ImGuiCol_TabHovered]           = ImVec4(0.38f, 0.38f, 0.39f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline]  = ImVec4(0.17f, 0.17f, 0.18f, 1.00f);

    colors[ImGuiCol_CheckMark]            = accent;
    colors[ImGuiCol_SliderGrab]           = accent;
    colors[ImGuiCol_SliderGrabActive]     = accentAct;
    colors[ImGuiCol_Button]               = ImVec4(accent.x * 0.86f, accent.y * 0.86f, accent.z * 0.86f, 1.00f);
    colors[ImGuiCol_ButtonHovered]        = accentHov;
    colors[ImGuiCol_ButtonActive]         = accentAct;

    colors[ImGuiCol_Header]               = ImVec4(0.38f, 0.38f, 0.39f, 0.75f);
    colors[ImGuiCol_HeaderHovered]        = ImVec4(0.38f, 0.38f, 0.39f, 1.00f);
    colors[ImGuiCol_HeaderActive]         = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

    colors[ImGuiCol_Separator]            = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.51f, 0.50f, 0.50f, 1.00f);
    colors[ImGuiCol_SeparatorActive]      = ImVec4(0.61f, 0.60f, 0.62f, 1.00f);
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.26f, 0.25f, 0.26f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.40f, 0.39f, 0.41f, 0.70f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);

    colors[ImGuiCol_ModalWindowDimBg]     = ImVec4(0.02f, 0.01f, 0.03f, 0.60f);

    colors[ImGuiCol_PlotLines]            = ImVec4(0.76f, 0.87f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]     = ImVec4(1.00f, 0.88f, 0.05f, 1.00f);
    colors[ImGuiCol_DockingPreview]       = ImVec4(0.67f, 0.67f, 0.68f, 0.70f);
}

void ThemeEditorMenu() {
    if (!showThemeEditor)
        return;

    ImGui::Begin("Theme Editor", &showThemeEditor);

    ImGuiStyle& style = ImGui::GetStyle();

    static ImGuiTextFilter filter;
    filter.Draw("Filter");

    ImGui::Separator();

    ImGui::BeginChild("Colors");

    for (int i = 0; i < ImGuiCol_COUNT; i++) {
        const char* name = ImGui::GetStyleColorName(i);

        if (!filter.PassFilter(name))
            continue;

        ImGui::ColorEdit4(name, (float*)&style.Colors[i]);
    }

    ImGui::EndChild();

    if (ImGui::Button("Reset Changes")) {
        ImGuiStyle& style = ImGui::GetStyle();

        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            style.Colors[i] = originalThemeColors[i];
        }
    }

    ImGui::End();
}

void HandleKeyboardShortcuts() {
    ImGuiIO& io = ImGui::GetIO();

    // Do NOT bail out when io.WantTextInput is true.
    // The old guard caused every Ctrl+S/O/N/W/etc. to be silently dropped whenever
    // the editor had keyboard focus — i.e., always during normal use.
    // Ctrl-key combos are application shortcuts and must be processed regardless.
    // Pure text input (non-modifier keys) is handled by ImGui widgets themselves,
    // so no filtering is needed here.

    // Ctrl+F - Open Folder
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) {
        std::string folderpath = OpenFolderDialog();
        if (!folderpath.empty()) OpenFolder(folderpath);
    }
    
    // Ctrl+O - Open File
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        std::string filepath = OpenFileDialog();
        if (!filepath.empty()) OpenFile(filepath);
    }

    // Ctrl+N - New file / tab
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        FileTab t;
        t.filePath = "";
        t.content = "";
        t.isModified = false;
        g_appState.tabs.push_back(std::move(t));
        g_appState.activeTab = (int)g_appState.tabs.size() - 1;
        g_appState.focusEditor = true;
    }

    // Ctrl+S - Save active
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (g_appState.activeTab >= 0) SaveFile(g_appState.activeTab);
    }

    // Ctrl+Shift+S - Save As
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (g_appState.activeTab >= 0) SaveFileAs(g_appState.activeTab);
    }

    // Ctrl+W - Close active tab
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
        if (g_appState.activeTab >= 0) {
            if (g_appState.tabs[g_appState.activeTab].isModified) {
                g_appState.closeTabIndex = g_appState.activeTab;
            } else {
                CloseTab(g_appState.activeTab);
            }
        }
    }

}


void SetupInitialDockingLayout() {
    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");

    if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr) {
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->WorkSize);
        
        ImGuiID dock_main_id = dockspace_id;
        ImGuiID dock_left, dock_right;
        ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.20f, &dock_left, &dock_right);

        // Previously, I claimed a top/bottom split was performed, but dock_right was
        // never actually split — both "Files" and "Editor" were placed into the same node,
        // making them tabs instead of a vertical stack.
        // Removed the "Files" thingy

        ImGui::DockBuilderDockWindow("Explorer", dock_left);
        ImGui::DockBuilderDockWindow("Editor",   dock_right);

        ImGui::DockBuilderFinish(dockspace_id);
    }
}

void RenderMainDockSpace() {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    // Removed ImGuiWindowFlags_MenuBar. The dockspace host window does not
    // contain a BeginMenuBar/EndMenuBar block, so advertising that flag created a
    // blank, wasted menu-bar-sized gap at the top of the dockspace window.
    // The real menu bar is drawn by ImGui::BeginMainMenuBar() in RenderMenuBar().
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("MainDockSpace", nullptr, window_flags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (g_appState.firstRun) {
        SetupInitialDockingLayout();
        g_appState.firstRun = false;
    }

    ImGui::End();
}

void RenderMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open Folder", "Ctrl+F")) {
                std::string folder = OpenFolderDialog();
                if (!folder.empty()) {
                    OpenFolder(folder);
                }
            }
            if (ImGui::MenuItem("Open File", "Ctrl+O")) {
                std::string path = OpenFileDialog();
                if (!path.empty()) OpenFile(path);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("New File", "Ctrl+N")) {
                FileTab t;
                t.filePath = "";
                t.content = "";
                t.isModified = false;
                g_appState.tabs.push_back(std::move(t));
                g_appState.activeTab = (int)g_appState.tabs.size() - 1;
                g_appState.focusEditor = true;
            }

            if (ImGui::MenuItem("Save", "Ctrl+S", false, g_appState.activeTab >= 0)) {
                if (g_appState.activeTab >= 0) SaveFile(g_appState.activeTab);
            }
            if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S", false, g_appState.activeTab >= 0)) {
                if (g_appState.activeTab >= 0) SaveFileAs(g_appState.activeTab);
            }
            if (ImGui::MenuItem("Save All")) {
                SaveAll();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                // Use the global g_window handle instead of glfwGetCurrentContext().
                // glfwGetCurrentContext() returns the OpenGL context, not necessarily the
                // GLFW window we want to close — they can diverge when viewports are active
                // and ImGui temporarily switches contexts for secondary windows.
                glfwSetWindowShouldClose(g_window, GLFW_TRUE);
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Dark", nullptr, currentTheme == THEME_DARK)) {
                    ImGui::StyleColorsDark();
                    currentTheme = THEME_DARK;
                }
                if (ImGui::MenuItem("Light", nullptr, currentTheme == THEME_LIGHT)) {
                    ImGui::StyleColorsLight();
                    currentTheme = THEME_LIGHT;
                }
                if (ImGui::MenuItem("Grey", nullptr, currentTheme == THEME_GREY)) {
                    SetupInitialStyle();
                    currentTheme = THEME_GREY;
                }
                if (ImGui::MenuItem("Custom Mode", nullptr, currentTheme == THEME_CUSTOM)) {

                    if (!themeBackupSaved) {
                        ImGuiStyle& style = ImGui::GetStyle();

                        for (int i = 0; i < ImGuiCol_COUNT; i++) {
                            originalThemeColors[i] = style.Colors[i];
                        }

                        themeBackupSaved = true;
                    }

                    showThemeEditor = true;
                    currentTheme = THEME_CUSTOM;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About")) {
                g_appState.showAboutDialog = true;
            }
            ImGui::EndMenu();
        }

        if (g_appState.needsSave) {
            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 160);
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "[Unsaved Changes]");
        }

        ImGui::EndMainMenuBar();
    }
}

void RenderEditor() {
    ImGui::Begin("Editor");

    // Render tab bar at the top
    if (!g_appState.tabs.empty()) {
        if (ImGui::BeginTabBar("EditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyScroll)) {
            int closeTabRequest = -1;
            
            for (int i = 0; i < (int)g_appState.tabs.size(); ++i) {
                FileTab &tab = g_appState.tabs[i];
                
                std::string tabLabel = tab.filePath.empty() 
                    ? ("Untitled " + std::to_string(i + 1))
                    : fs::path(tab.filePath).filename().string();
                
                if (tab.isModified) {
                    tabLabel = "• " + tabLabel;  // Filled circle for modified
                }
                
                ImGui::PushID(i);
                bool tabOpen = true;
                
                // Let ImGui handle tab selection - don't use SetSelected flag
                if (ImGui::BeginTabItem(tabLabel.c_str(), &tabOpen)) {
                    // Update our state when this tab is selected
                    if (g_appState.activeTab != i) {
                        g_appState.activeTab = i;
                        g_appState.lastActiveTab = -1;  // Force focus on next frame
                    }
                    ImGui::EndTabItem();
                }
                
                ImGui::PopID();
                
                if (!tabOpen) {
                    closeTabRequest = i;
                }
            }
            
            ImGui::EndTabBar();
            
            // Handle tab close after the loop to avoid iterator invalidation
            if (closeTabRequest >= 0) {
                if (g_appState.tabs[closeTabRequest].isModified) {
                    g_appState.closeTabIndex = closeTabRequest;
                } else {
                    CloseTab(closeTabRequest);
                }
            }
        }
    }

    if (g_appState.activeTab >= 0 && g_appState.activeTab < (int)g_appState.tabs.size()) {
        FileTab &tab = g_appState.tabs[g_appState.activeTab];

        ImVec2 availSize = ImGui::GetContentRegionAvail();
        // Clamp editor height so it cannot go negative when the panel is
        // smaller than the reserved space for the toolbar/status row below it.
        availSize.y = std::max(availSize.y - 80.0f, 1.0f);

        // Only set keyboard focus when switching tabs, not every frame
        if (g_appState.activeTab != g_appState.lastActiveTab) {
            ImGui::SetKeyboardFocusHere();
            g_appState.lastActiveTab = g_appState.activeTab;
        }

        // Use resize() instead of reserve() so that size() == capacity() and
        // ImGui can safely write up to capacity()-1 bytes via the data() pointer.
        // reserve() only allocates memory without extending size(), making writes past
        // size() undefined behavior even though the memory is technically allocated.
        const size_t requiredCapacity = std::max(tab.content.size() * 2 + 1, size_t(4096));
        if (tab.editBuffer.size() < requiredCapacity) {
            tab.editBuffer.resize(requiredCapacity, '\0');
            std::copy(tab.content.begin(), tab.content.end(), tab.editBuffer.begin());
            tab.editBuffer[tab.content.size()] = '\0';
        } else if (tab.editBuffer.empty() ||
                   std::string(tab.editBuffer.data()) != tab.content) {
            // Re-sync if content was changed externally (e.g. Revert).
            std::copy(tab.content.begin(), tab.content.end(), tab.editBuffer.begin());
            tab.editBuffer[tab.content.size()] = '\0';
        }

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;

        if (ImGui::InputTextMultiline(
                "##editor",
                tab.editBuffer.data(),
                tab.editBuffer.size(),   // pass size(), not capacity()
                availSize,
                flags))
        {
            std::string newContent(tab.editBuffer.data());
            if (newContent != tab.content) {
                tab.content = newContent;
                tab.isModified = true;
                g_appState.needsSave = true;
                UpdateFileStats(tab);
            }
        }

        ImGui::Separator();

        if (ImGui::Button("Save", ImVec2(100, 0))) {
            SaveFile(g_appState.activeTab);
        }
        ImGui::SameLine();
        if (ImGui::Button("Save As", ImVec2(100, 0))) {
            SaveFileAs(g_appState.activeTab);
        }
        ImGui::SameLine();
        if (ImGui::Button("Revert", ImVec2(100, 0))) {
            if (!tab.filePath.empty() && fs::exists(tab.filePath)) {
                tab.content = ReadFileContent(tab.filePath);
                tab.lastModified = fs::last_write_time(tab.filePath);
                tab.isModified = false;
                tab.editBuffer.clear(); // Clear buffer to force re-sync next frame
                UpdateFileStats(tab);
            } else {
                tab.content.clear();
                tab.isModified = false;
                tab.editBuffer.clear();
                UpdateFileStats(tab);
            }
        }

        ImGui::SameLine();
        ImGui::Text("Words: %d | Characters: %zu", tab.cachedWordCount, tab.cachedCharCount);

    } else {
        ImVec2 windowSize = ImGui::GetWindowSize();
        ImVec2 textSize = ImGui::CalcTextSize("Open a file or create a new file");
        ImGui::SetCursorPos(ImVec2((windowSize.x - textSize.x) * 0.5f, windowSize.y * 0.4f));
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Open a file or create a new file");
        
        ImGui::SetCursorPos(ImVec2((windowSize.x - 300) * 0.5f, windowSize.y * 0.5f));
        if (ImGui::Button("New File", ImVec2(140, 0))) {
            FileTab t;
            t.filePath = "";
            t.content = "";
            t.isModified = false;
            g_appState.tabs.push_back(std::move(t));
            g_appState.activeTab = (int)g_appState.tabs.size() - 1;
            g_appState.focusEditor = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open File", ImVec2(140, 0))) {
            std::string path = OpenFileDialog();
            if (!path.empty()) OpenFile(path);
        }
    }

    ImGui::End();
}


void RenderSimpleFileBrowser() {
    if (!g_appState.showFileDialog) return;

    ImGui::OpenPopup("File Browser");
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    
    if (ImGui::BeginPopupModal("File Browser", &g_appState.showFileDialog)) {
        if (g_appState.currentPath.empty()) {
            g_appState.currentPath = fs::current_path().string();
        }

        ImGui::Text("Current Path: %s", g_appState.currentPath.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Up")) {
            fs::path parent = fs::path(g_appState.currentPath).parent_path();
            if (!parent.empty()) {
                g_appState.currentPath = parent.string();
            }
        }

        ImGui::Separator();

        ImGui::BeginChild("FileList", ImVec2(0, -60));
        try {
            for (const auto& entry : fs::directory_iterator(g_appState.currentPath)) {
                std::string name = entry.path().filename().string();
                if (entry.is_directory()) {
                    if (ImGui::Selectable(("[DIR] " + name).c_str())) {
                        g_appState.currentPath = entry.path().string();
                    }
                } else {
                    if (ImGui::Selectable(name.c_str())) {
                        strncpy(g_appState.filePathBuffer, entry.path().string().c_str(), 
                               sizeof(g_appState.filePathBuffer) - 1);
                        g_appState.filePathBuffer[sizeof(g_appState.filePathBuffer) - 1] = '\0';
                    }
                }
            }
        } catch (const std::exception& e) {
            ImGui::Text("Error reading directory: %s", e.what());
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::InputText("File", g_appState.filePathBuffer, sizeof(g_appState.filePathBuffer));

        if (ImGui::Button("Open")) {
            if (strlen(g_appState.filePathBuffer) > 0) {
                OpenFile(g_appState.filePathBuffer);
                g_appState.showFileDialog = false;
                g_appState.filePathBuffer[0] = '\0';
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            g_appState.showFileDialog = false;
            g_appState.filePathBuffer[0] = '\0';
        }

        ImGui::EndPopup();
    }
}

void RenderDialogs() {
    if (g_appState.closeTabIndex >= 0) {
        ImGui::OpenPopup("Unsaved Changes");
    }

    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("This file has unsaved changes.");
        ImGui::Text("Do you want to save before closing?");
        ImGui::Separator();
        
        if (ImGui::Button("Save", ImVec2(120, 0))) {
            if (g_appState.closeTabIndex >= 0 && g_appState.closeTabIndex < (int)g_appState.tabs.size()) {
                SaveFile(g_appState.closeTabIndex);
                CloseTab(g_appState.closeTabIndex);
            }
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
            if (g_appState.closeTabIndex >= 0 && g_appState.closeTabIndex < (int)g_appState.tabs.size()) {
                CloseTab(g_appState.closeTabIndex);
            }
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        
        if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            g_appState.closeTabIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    if (g_appState.showAboutDialog) {
        ImGui::OpenPopup("About Edifier");
        g_appState.showAboutDialog = false;
    }

    if (ImGui::BeginPopupModal("About Edifier", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Edifier - File Editor");
        ImGui::Separator();

        ImGui::Text("Version: 1.0.0");
        ImGui::Text("Built with C++ and Dear ImGui");
        ImGui::Separator();
        
        ImGui::Text("Features:");
        ImGui::BulletText("Open and edit any file type");
        ImGui::BulletText("Multiple file tabs");
        ImGui::BulletText("Fast search through files");
        ImGui::BulletText("Keyboard shortcuts");
        ImGui::BulletText("Auto-save indicator");
        ImGui::BulletText("Dockable interface");
        
        ImGui::Separator();
        ImGui::Text("Keyboard Shortcuts:");
        ImGui::BulletText("Ctrl+F: Open folders");
        ImGui::BulletText("Ctrl+O: Open file");
        ImGui::BulletText("Ctrl+N: New file");
        ImGui::BulletText("Ctrl+S: Save");
        ImGui::BulletText("Ctrl+Shift+S: Save as");
        ImGui::BulletText("Ctrl+W: Close tab");
        
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }

    RenderSimpleFileBrowser();
}


int main() {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW\n";
        return -1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    // Store window in the global so all functions can reference it safely.
    g_window = glfwCreateWindow(1400, 900, "", nullptr, nullptr);
    if (!g_window) {
        std::cerr << "Failed to create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    setEmbeddedIcon(g_window);

    glfwMakeContextCurrent(g_window);
    glfwSwapInterval(1); // Enable Vsync for now.

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cerr << "Failed to initialize GLAD\n";
        glfwDestroyWindow(g_window);
        glfwTerminate();
        return -1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImFont* defaultFont = io.Fonts->AddFontDefault();

    

    ImFont* mainFont = io.Fonts->AddFontFromFileTTF("fonts/JetBrainsMonoNerdFontMono-Bold.ttf", 16.0f);

    io.FontDefault = mainFont;

    SetupInitialStyle();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplGlfw_InitForOpenGL(g_window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Initialize GTK once here at startup, not per-dialog.
    gtkInit();

    while (!glfwWindowShouldClose(g_window)) {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        HandleKeyboardShortcuts();

        RenderMainDockSpace();
        RenderMenuBar();
        ThemeEditorMenu();
        RenderEditor();
        RenderExplorer();
        RenderDialogs();

        ImGui::Render();

        int display_w, display_h;
        glfwGetFramebufferSize(g_window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(g_window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(g_window);
    glfwTerminate();
    
    gtkCleanup();

    return 0;
}
