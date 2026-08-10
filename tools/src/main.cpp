#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <tchar.h>
#include <windows.h>
#include <commdlg.h>
#include <wincodec.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace {

constexpr int kMaxLayouts = 32;
constexpr int kNdsAspectW = 4;
constexpr int kNdsAspectH = 3;

struct RectI {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

struct Layout {
    int index = 0;
    int type = 0;
    std::string name = "Layout";
    std::string bg;
    int rotate = 0;
    RectI screen[2];
};

struct Document {
    std::string name = "Drastic Layout";
    int width = 1920;
    int height = 1080;
    std::vector<Layout> layouts;
};

struct JsonValue {
    enum class Type {
        Null,
        Object,
        Array,
        String,
        Number,
        Boolean
    };

    Type type = Type::Null;
    std::map<std::string, JsonValue> object;
    std::vector<JsonValue> array;
    std::string string_value;
    double number_value = 0.0;
    bool bool_value = false;
};

class JsonParser {
public:
    explicit JsonParser(const std::string &text) : text_(text) {}

    bool Parse(JsonValue *out, std::string *error)
    {
        SkipSpace();
        if (!ParseValue(out)) {
            SetError("Invalid JSON value", error);
            return false;
        }
        SkipSpace();
        if (pos_ != text_.size()) {
            SetError("Unexpected trailing data", error);
            return false;
        }
        return true;
    }

private:
    bool ParseValue(JsonValue *out)
    {
        SkipSpace();
        if (pos_ >= text_.size()) {
            return false;
        }

        const char c = text_[pos_];
        if (c == '{') {
            return ParseObject(out);
        }
        if (c == '[') {
            return ParseArray(out);
        }
        if (c == '"') {
            out->type = JsonValue::Type::String;
            return ParseString(&out->string_value);
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            out->type = JsonValue::Type::Number;
            return ParseNumber(&out->number_value);
        }
        if (ConsumeLiteral("true")) {
            out->type = JsonValue::Type::Boolean;
            out->bool_value = true;
            return true;
        }
        if (ConsumeLiteral("false")) {
            out->type = JsonValue::Type::Boolean;
            out->bool_value = false;
            return true;
        }
        if (ConsumeLiteral("null")) {
            out->type = JsonValue::Type::Null;
            return true;
        }
        return false;
    }

    bool ParseObject(JsonValue *out)
    {
        if (!Consume('{')) {
            return false;
        }
        out->type = JsonValue::Type::Object;
        out->object.clear();
        SkipSpace();
        if (Consume('}')) {
            return true;
        }

        while (pos_ < text_.size()) {
            std::string key;
            JsonValue value;
            SkipSpace();
            if (!ParseString(&key)) {
                return false;
            }
            SkipSpace();
            if (!Consume(':')) {
                return false;
            }
            if (!ParseValue(&value)) {
                return false;
            }
            out->object[key] = value;
            SkipSpace();
            if (Consume('}')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
        }
        return false;
    }

    bool ParseArray(JsonValue *out)
    {
        if (!Consume('[')) {
            return false;
        }
        out->type = JsonValue::Type::Array;
        out->array.clear();
        SkipSpace();
        if (Consume(']')) {
            return true;
        }

        while (pos_ < text_.size()) {
            JsonValue item;
            if (!ParseValue(&item)) {
                return false;
            }
            out->array.push_back(item);
            SkipSpace();
            if (Consume(']')) {
                return true;
            }
            if (!Consume(',')) {
                return false;
            }
        }
        return false;
    }

    bool ParseString(std::string *out)
    {
        if (!Consume('"')) {
            return false;
        }
        out->clear();
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    return false;
                }
                const char esc = text_[pos_++];
                switch (esc) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u':
                    if (pos_ + 4 > text_.size()) {
                        return false;
                    }
                    pos_ += 4;
                    out->push_back('?');
                    break;
                default:
                    return false;
                }
            } else {
                out->push_back(c);
            }
        }
        return false;
    }

    bool ParseNumber(double *out)
    {
        const size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= text_.size()) {
            return false;
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            if (!std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() || !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                return false;
            }
            while (pos_ < text_.size() && std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        *out = std::strtod(text_.c_str() + start, nullptr);
        return true;
    }

    bool Consume(char c)
    {
        SkipSpace();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool ConsumeLiteral(const char *literal)
    {
        const size_t len = std::strlen(literal);
        if (text_.compare(pos_, len, literal) == 0) {
            pos_ += len;
            return true;
        }
        return false;
    }

    void SkipSpace()
    {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    void SetError(const char *message, std::string *error) const
    {
        if (!error) {
            return;
        }
        std::ostringstream oss;
        oss << message << " near byte " << pos_;
        *error = oss.str();
    }

    const std::string &text_;
    size_t pos_ = 0;
};

const JsonValue *FindMember(const JsonValue &obj, const char *name)
{
    if (obj.type != JsonValue::Type::Object) {
        return nullptr;
    }
    const auto it = obj.object.find(name);
    return it == obj.object.end() ? nullptr : &it->second;
}

std::string GetString(const JsonValue &obj, const char *name, const std::string &fallback)
{
    const JsonValue *value = FindMember(obj, name);
    return value && value->type == JsonValue::Type::String ? value->string_value : fallback;
}

int GetInt(const JsonValue &obj, const char *name, int fallback)
{
    const JsonValue *value = FindMember(obj, name);
    return value && value->type == JsonValue::Type::Number ? static_cast<int>(std::lround(value->number_value)) : fallback;
}

std::string EscapeJson(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                escaped += buf;
            } else {
                escaped.push_back(static_cast<char>(c));
            }
        }
    }
    return escaped;
}

std::wstring Utf8ToWide(const std::string &value)
{
    if (value.empty()) {
        return std::wstring();
    }
    const int len = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, nullptr, 0);
    if (len <= 0) {
        return std::wstring();
    }
    std::wstring wide(static_cast<size_t>(len - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), -1, wide.data(), len);
    return wide;
}

std::string WideToUtf8(const wchar_t *data, int len)
{
    if (!data || len <= 0) {
        return std::string();
    }
    const int out_len = WideCharToMultiByte(CP_UTF8, 0, data, len, nullptr, 0, nullptr, nullptr);
    if (out_len <= 0) {
        return std::string();
    }
    std::string out(static_cast<size_t>(out_len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, data, len, out.data(), out_len, nullptr, nullptr);
    return out;
}

bool DecodeJsonText(const std::string &bytes, std::string *text, std::string *error)
{
    if (bytes.empty()) {
        *error = "JSON file is empty";
        return false;
    }

    const unsigned char *data = reinterpret_cast<const unsigned char *>(bytes.data());
    const size_t size = bytes.size();

    if (size >= 3 && data[0] == 0xEF && data[1] == 0xBB && data[2] == 0xBF) {
        *text = bytes.substr(3);
        return true;
    }

    if (size >= 2 && data[0] == 0xFF && data[1] == 0xFE) {
        const int wchar_count = static_cast<int>((size - 2) / 2);
        *text = WideToUtf8(reinterpret_cast<const wchar_t *>(bytes.data() + 2), wchar_count);
        return true;
    }

    if (size >= 2 && data[0] == 0xFE && data[1] == 0xFF) {
        std::wstring wide;
        wide.reserve((size - 2) / 2);
        for (size_t i = 2; i + 1 < size; i += 2) {
            wide.push_back(static_cast<wchar_t>((data[i] << 8) | data[i + 1]));
        }
        *text = WideToUtf8(wide.data(), static_cast<int>(wide.size()));
        return true;
    }

    if (size >= 4 && data[0] == '{' && data[1] == 0x00) {
        const int wchar_count = static_cast<int>(size / 2);
        *text = WideToUtf8(reinterpret_cast<const wchar_t *>(bytes.data()), wchar_count);
        return true;
    }

    *text = bytes;
    return true;
}

std::filesystem::path LayoutDirectory(const std::string &layout_path)
{
    std::filesystem::path path(layout_path);
    std::filesystem::path parent = path.parent_path();
    return parent.empty() ? std::filesystem::path(".") : parent;
}

std::filesystem::path ThemeBackgroundPath(const std::string &layout_path, const Layout &layout)
{
    if (layout.bg.empty()) {
        return std::filesystem::path();
    }
    return LayoutDirectory(layout_path) / "1" / layout.bg;
}

void NormalizeBackgroundName(Layout *layout)
{
    if (layout->bg.empty()) {
        return;
    }

    std::filesystem::path bg_path(layout->bg);
    if (!bg_path.has_extension()) {
        layout->bg += ".png";
    }
}

std::string SanitizeFileStem(const std::string &name, int fallback_index)
{
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(std::tolower(c)));
        } else if (c == '-' || c == '_' || c == ' ') {
            if (out.empty() || out.back() != '_') {
                out.push_back('_');
            }
        }
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    if (out.empty()) {
        out = "layout_" + std::to_string(fallback_index);
    }
    return out;
}

void EnsureDefaultBackgroundNames(Document *doc)
{
    std::map<std::string, int> used;
    for (Layout &layout : doc->layouts) {
        if (layout.bg.empty()) {
            std::string stem = SanitizeFileStem(layout.name, layout.index);
            int &count = used[stem];
            if (count > 0) {
                stem += "_" + std::to_string(count + 1);
            }
            ++count;
            layout.bg = stem + ".png";
        }
        NormalizeBackgroundName(&layout);
    }
}

void NormalizeIndices(Document *doc)
{
    for (size_t i = 0; i < doc->layouts.size(); ++i) {
        doc->layouts[i].index = static_cast<int>(i);
    }
}

void InferResolutionFromPath(const std::string &path, Document *doc)
{
    const size_t file_sep = path.find_last_of("/\\");
    if (file_sep == std::string::npos) {
        return;
    }
    const size_t dir_end = file_sep;
    const size_t dir_start = path.find_last_of("/\\", dir_end == 0 ? 0 : dir_end - 1);
    const std::string folder = path.substr(dir_start == std::string::npos ? 0 : dir_start + 1,
                                           dir_end - (dir_start == std::string::npos ? 0 : dir_start + 1));
    int w = 0;
    int h = 0;
    if (std::sscanf(folder.c_str(), "%dx%d", &w, &h) == 2 && w > 0 && h > 0) {
        doc->width = w;
        doc->height = h;
    }
}

bool LoadLayoutFile(const std::string &path, Document *doc, std::string *error)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "Failed to open file";
        return false;
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();

    std::string json_text;
    if (!DecodeJsonText(buffer.str(), &json_text, error)) {
        return false;
    }

    JsonValue root;
    JsonParser parser(json_text);
    if (!parser.Parse(&root, error)) {
        return false;
    }
    if (root.type != JsonValue::Type::Object) {
        *error = "Root must be a JSON object";
        return false;
    }

    const JsonValue *layouts = FindMember(root, "layout");
    if (!layouts || layouts->type != JsonValue::Type::Array) {
        *error = "Missing layout array";
        return false;
    }

    Document loaded;
    loaded.name = GetString(root, "name", "Drastic Layout");
    InferResolutionFromPath(path, &loaded);

    const size_t count = std::min(layouts->array.size(), static_cast<size_t>(kMaxLayouts));
    for (size_t i = 0; i < count; ++i) {
        const JsonValue &item = layouts->array[i];
        if (item.type != JsonValue::Type::Object) {
            continue;
        }

        Layout layout;
        layout.index = GetInt(item, "index", static_cast<int>(loaded.layouts.size()));
        layout.type = GetInt(item, "type", 0);
        layout.name = GetString(item, "name", "Layout");
        layout.bg = GetString(item, "bg", "");
        layout.rotate = GetInt(item, "rotate", 0);
        layout.screen[0].x = GetInt(item, "screen0_x", 0);
        layout.screen[0].y = GetInt(item, "screen0_y", 0);
        layout.screen[0].w = GetInt(item, "screen0_w", 0);
        layout.screen[0].h = GetInt(item, "screen0_h", 0);
        layout.screen[1].x = GetInt(item, "screen1_x", 0);
        layout.screen[1].y = GetInt(item, "screen1_y", 0);
        layout.screen[1].w = GetInt(item, "screen1_w", 0);
        layout.screen[1].h = GetInt(item, "screen1_h", 0);
        loaded.layouts.push_back(layout);
    }

    if (loaded.layouts.empty()) {
        *error = "No valid layout entries found";
        return false;
    }

    NormalizeIndices(&loaded);
    *doc = loaded;
    return true;
}

bool SaveLayoutFile(const std::string &path, Document doc, std::string *error)
{
    NormalizeIndices(&doc);
    EnsureDefaultBackgroundNames(&doc);

    {
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                *error = "Failed to create output directory: " + ec.message();
                return false;
            }
        }
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        *error = "Failed to create file";
        return false;
    }

    out << "{\n";
    out << "  \"name\": \"" << EscapeJson(doc.name) << "\",\n";
    out << "  \"layout\": [\n";
    for (size_t i = 0; i < doc.layouts.size(); ++i) {
        const Layout &layout = doc.layouts[i];
        out << "    {\n";
        out << "      \"index\": " << layout.index << ",\n";
        out << "      \"type\": " << layout.type << ",\n";
        out << "      \"name\": \"" << EscapeJson(layout.name) << "\",\n";
        out << "      \"bg\": \"" << EscapeJson(layout.bg) << "\",\n";
        out << "      \"rotate\": " << layout.rotate << ",\n";
        out << "      \"screen0_x\": " << layout.screen[0].x << ",\n";
        out << "      \"screen0_y\": " << layout.screen[0].y << ",\n";
        out << "      \"screen0_w\": " << layout.screen[0].w << ",\n";
        out << "      \"screen0_h\": " << layout.screen[0].h << ",\n";
        out << "      \"screen1_x\": " << layout.screen[1].x << ",\n";
        out << "      \"screen1_y\": " << layout.screen[1].y << ",\n";
        out << "      \"screen1_w\": " << layout.screen[1].w << ",\n";
        out << "      \"screen1_h\": " << layout.screen[1].h << "\n";
        out << "    }" << (i + 1 == doc.layouts.size() ? "\n" : ",\n");
    }
    out << "  ]\n";
    out << "}\n";

    if (!out) {
        *error = "Failed while writing file";
        return false;
    }
    return true;
}

RectI FitAspect(int box_x, int box_y, int box_w, int box_h)
{
    RectI r;
    r.w = box_w;
    r.h = r.w * kNdsAspectH / kNdsAspectW;
    if (r.h > box_h) {
        r.h = box_h;
        r.w = r.h * kNdsAspectW / kNdsAspectH;
    }
    r.x = box_x + (box_w - r.w) / 2;
    r.y = box_y + (box_h - r.h) / 2;
    return r;
}

Layout MakeDefaultLayout(const Document &doc, const char *name)
{
    Layout layout;
    layout.name = name;
    layout.type = 0;

    const int left_w = doc.width / 2;
    const int right_w = doc.width - left_w;
    layout.screen[0] = FitAspect(0, 0, left_w, doc.height);
    layout.screen[1] = FitAspect(left_w, 0, right_w, doc.height);
    return layout;
}

void ApplyTemplate(Document *doc, Layout *layout, int template_id)
{
    const std::string name = layout->name;
    const std::string bg = layout->bg;
    const int gap = std::max(8, doc->width / 48);
    layout->rotate = 0;

    switch (template_id) {
    case 0:
        *layout = MakeDefaultLayout(*doc, name.c_str());
        layout->bg = bg;
        layout->type = 0;
        break;
    case 1:
        layout->type = 0;
        {
            const int top_h = doc->height / 2;
            const int bottom_h = doc->height - top_h;
            layout->screen[0] = FitAspect(0, 0, doc->width, top_h);
            layout->screen[1] = FitAspect(0, top_h, doc->width, bottom_h);
        }
        break;
    case 2:
        layout->type = 0;
        layout->screen[0].h = doc->height;
        layout->screen[0].w = layout->screen[0].h * kNdsAspectW / kNdsAspectH;
        if (layout->screen[0].w > doc->width) {
            layout->screen[0].w = doc->width;
            layout->screen[0].h = layout->screen[0].w * kNdsAspectH / kNdsAspectW;
        }
        layout->screen[0].x = 0;
        layout->screen[0].y = (doc->height - layout->screen[0].h) / 2;
        {
            const int side_x = layout->screen[0].x + layout->screen[0].w;
            const int side_w = std::max(0, doc->width - side_x);
            if (side_w > 0) {
                layout->screen[1].w = std::max(1, std::min(side_w, layout->screen[0].w / 3));
                layout->screen[1].h = layout->screen[1].w * kNdsAspectH / kNdsAspectW;
                if (layout->screen[1].h > doc->height) {
                    layout->screen[1].h = doc->height;
                    layout->screen[1].w = layout->screen[1].h * kNdsAspectW / kNdsAspectH;
                }
                layout->screen[1].x = side_x + (side_w - layout->screen[1].w) / 2;
                layout->screen[1].y = (doc->height - layout->screen[1].h) / 2;
            } else {
                layout->screen[1].w = std::max(1, doc->width / 4);
                layout->screen[1].h = layout->screen[1].w * kNdsAspectH / kNdsAspectW;
                layout->screen[1].x = doc->width - gap - layout->screen[1].w;
                layout->screen[1].y = doc->height - gap - layout->screen[1].h;
            }
        }
        break;
    case 3:
        layout->type = 1;
        layout->screen[0] = FitAspect(0, 0, doc->width, doc->height);
        layout->screen[1].w = std::max(1, layout->screen[0].w / 3);
        layout->screen[1].h = layout->screen[1].w * kNdsAspectH / kNdsAspectW;
        layout->screen[1].x = 0;
        layout->screen[1].y = 0;
        break;
    case 4:
        layout->type = 4;
        layout->screen[0] = FitAspect(0, 0, doc->width, doc->height);
        layout->screen[1] = {};
        break;
    default:
        break;
    }
}

void EnsureDefaultDocument(Document *doc)
{
    if (!doc->layouts.empty()) {
        return;
    }
    doc->layouts.push_back(MakeDefaultLayout(*doc, "Normal"));
    Layout vertical = doc->layouts[0];
    vertical.name = "Vertical";
    ApplyTemplate(doc, &vertical, 1);
    doc->layouts.push_back(vertical);
    Layout top_full = doc->layouts[0];
    top_full.name = "Top Full";
    ApplyTemplate(doc, &top_full, 2);
    doc->layouts.push_back(top_full);
    Layout transparent = doc->layouts[0];
    transparent.name = "Transparent";
    ApplyTemplate(doc, &transparent, 3);
    doc->layouts.push_back(transparent);
    NormalizeIndices(doc);
}

bool InputTextString(const char *label, std::string *value)
{
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", value->c_str());
    if (ImGui::InputText(label, buffer, sizeof(buffer))) {
        *value = buffer;
        return true;
    }
    return false;
}

void ClampRect(RectI *rect, int width, int height)
{
    rect->w = std::max(0, rect->w);
    rect->h = std::max(0, rect->h);
    if (rect->w > width) {
        rect->w = width;
    }
    if (rect->h > height) {
        rect->h = height;
    }
    rect->x = std::max(0, std::min(rect->x, width - rect->w));
    rect->y = std::max(0, std::min(rect->y, height - rect->h));
}

bool IsQuarterRotated(int rotate)
{
    return rotate == 90 || rotate == 270;
}

RectI DisplayRect(const RectI &rect, int rotate)
{
    RectI out = rect;
    if (IsQuarterRotated(rotate)) {
        out.w = rect.h;
        out.h = rect.w;
    }
    return out;
}

void ClampRectForRotate(RectI *rect, int rotate, int width, int height)
{
    rect->w = std::max(0, rect->w);
    rect->h = std::max(0, rect->h);

    if (IsQuarterRotated(rotate)) {
        if (rect->h > width) {
            rect->h = width;
        }
        if (rect->w > height) {
            rect->w = height;
        }
        RectI display = DisplayRect(*rect, rotate);
        rect->x = std::max(0, std::min(rect->x, width - display.w));
        rect->y = std::max(0, std::min(rect->y, height - display.h));
    } else {
        ClampRect(rect, width, height);
    }
}

void KeepAspectFromWidth(RectI *rect)
{
    rect->w = std::max(0, rect->w);
    rect->h = rect->w * kNdsAspectH / kNdsAspectW;
}

void SetLockedAspectFromDisplayWidth(RectI *rect, int rotate, int desired_display_w, int canvas_w, int canvas_h)
{
    rect->x = std::max(0, std::min(rect->x, canvas_w - 1));
    rect->y = std::max(0, std::min(rect->y, canvas_h - 1));

    const int max_display_w = std::max(1, canvas_w - rect->x);
    const int max_display_h = std::max(1, canvas_h - rect->y);

    if (IsQuarterRotated(rotate)) {
        int display_w = std::max(1, desired_display_w);
        display_w = std::min(display_w, max_display_w);
        display_w = std::min(display_w, std::max(1, max_display_h * kNdsAspectH / kNdsAspectW));
        const int display_h = std::max(1, display_w * kNdsAspectW / kNdsAspectH);
        rect->w = display_h;
        rect->h = display_w;
    } else {
        int display_w = std::max(1, desired_display_w);
        display_w = std::min(display_w, max_display_w);
        display_w = std::min(display_w, std::max(1, max_display_h * kNdsAspectW / kNdsAspectH));
        rect->w = display_w;
        rect->h = std::max(1, display_w * kNdsAspectH / kNdsAspectW);
    }
}

void ClampLockedAspectToCanvas(RectI *rect, int rotate, int canvas_w, int canvas_h)
{
    SetLockedAspectFromDisplayWidth(rect, rotate, DisplayRect(*rect, rotate).w, canvas_w, canvas_h);
}

struct Interaction {
    int screen = -1;
    int mode = 0; // 1 move, 2 resize bottom-right
    ImVec2 start_mouse = {};
    RectI start_rect = {};
};

enum class UiLanguage {
    English,
    Chinese
};

UiLanguage DetectUiLanguage()
{
    const LANGID lang = GetUserDefaultUILanguage();
    return PRIMARYLANGID(lang) == LANG_CHINESE ? UiLanguage::Chinese : UiLanguage::English;
}

struct AppState {
    Document doc;
    int current_layout = 0;
    int selected_screen = 0;
    bool lock_aspect = true;
    bool allow_overlap = true;
    UiLanguage language = DetectUiLanguage();
    char file_path[1024] = "";
    std::string status = "Ready";
    Interaction interaction;
    ID3D11ShaderResourceView *bg_texture = nullptr;
    std::string bg_texture_path;
    int bg_texture_w = 0;
    int bg_texture_h = 0;
};

ID3D11Device *g_pd3d_device = nullptr;
ID3D11DeviceContext *g_pd3d_device_context = nullptr;
IDXGISwapChain *g_swap_chain = nullptr;
ID3D11RenderTargetView *g_main_render_target_view = nullptr;
HWND g_main_hwnd = nullptr;

const char *Tr(const AppState *state, const char *english, const char *chinese)
{
    return state->language == UiLanguage::Chinese ? chinese : english;
}

void UpdateWindowTitle(const AppState *state)
{
    if (!g_main_hwnd) {
        return;
    }
    const char *title = Tr(state,
                           "Drastic Layout Editor - Aveyondfly",
                           "Drastic Layout Editor - 微信公众号k源机");
    const std::wstring wide_title = Utf8ToWide(title);
    SetWindowTextW(g_main_hwnd, wide_title.c_str());
}

void LoadUiFonts(ImGuiIO *io)
{
    char windows_dir[MAX_PATH] = {};
    if (GetWindowsDirectoryA(windows_dir, sizeof(windows_dir)) == 0) {
        io->Fonts->AddFontDefault();
        return;
    }

    const std::vector<std::string> candidates = {
        std::string(windows_dir) + "\\Fonts\\msyh.ttc",
        std::string(windows_dir) + "\\Fonts\\simhei.ttf",
        std::string(windows_dir) + "\\Fonts\\simsun.ttc"
    };

    for (const std::string &path : candidates) {
        if (std::filesystem::exists(path)) {
            ImFontConfig config;
            config.OversampleH = 2;
            config.OversampleV = 2;
            io->Fonts->AddFontFromFileTTF(path.c_str(), 16.0f, &config, io->Fonts->GetGlyphRangesChineseFull());
            return;
        }
    }

    io->Fonts->AddFontDefault();
}

std::string SuggestedLayoutPath(int width, int height)
{
    std::ostringstream oss;
    oss << "resources/bg/" << width << "x" << height << "/layout.json";
    return oss.str();
}

std::string ExeDirectory()
{
    char path[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (len == 0 || len >= sizeof(path)) {
        return ".";
    }
    std::filesystem::path exe_path(path);
    return exe_path.parent_path().string();
}

bool BrowseLayoutJson(char *path, size_t path_size, bool save_dialog)
{
    char buffer[MAX_PATH] = {};
    std::string initial_dir = ExeDirectory();
    if (path && path[0] != '\0') {
        std::snprintf(buffer, sizeof(buffer), "%s", path);
    } else if (save_dialog) {
        std::snprintf(buffer, sizeof(buffer), "layout.json");
    }

    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = "layout.json\0layout.json\0JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.lpstrFile = buffer;
    ofn.nMaxFile = sizeof(buffer);
    ofn.lpstrDefExt = "json";
    ofn.lpstrInitialDir = initial_dir.c_str();
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    BOOL ok = FALSE;
    if (save_dialog) {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
        ok = GetSaveFileNameA(&ofn);
    } else {
        ofn.Flags |= OFN_FILEMUSTEXIST;
        ok = GetOpenFileNameA(&ofn);
    }

    if (!ok) {
        return false;
    }

    std::snprintf(path, path_size, "%s", buffer);
    return true;
}

std::string DefaultSaveLayoutPath(int width, int height)
{
    return (std::filesystem::path(ExeDirectory()) / SuggestedLayoutPath(width, height)).string();
}

bool PointInside(const ImVec2 &p, const RectI &r)
{
    return r.w > 0 && r.h > 0 &&
           p.x >= static_cast<float>(r.x) && p.x <= static_cast<float>(r.x + r.w) &&
           p.y >= static_cast<float>(r.y) && p.y <= static_cast<float>(r.y + r.h);
}

bool RectsOverlap(const RectI &a, const RectI &b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) {
        return false;
    }
    return a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

bool TryMoveOutOfOverlap(RectI *rect, const RectI &other, int rotate, int width, int height)
{
    if (!RectsOverlap(DisplayRect(*rect, rotate), DisplayRect(other, rotate))) {
        return true;
    }
    RectI display_rect = DisplayRect(*rect, rotate);
    if (rect->w <= 0 || rect->h <= 0 || display_rect.w > width || display_rect.h > height) {
        return false;
    }

    const RectI original = *rect;
    const RectI original_display = DisplayRect(original, rotate);
    const RectI other_display = DisplayRect(other, rotate);
    std::vector<RectI> candidates;
    auto add_candidate = [&](int x, int y) {
        RectI candidate = original;
        candidate.x = x;
        candidate.y = y;
        ClampRectForRotate(&candidate, rotate, width, height);
        candidates.push_back(candidate);
    };

    add_candidate(other_display.x + other_display.w, original_display.y);
    add_candidate(other_display.x - original_display.w, original_display.y);
    add_candidate(original_display.x, other_display.y + other_display.h);
    add_candidate(original_display.x, other_display.y - original_display.h);
    add_candidate(other_display.x + other_display.w, other_display.y + (other_display.h - original_display.h) / 2);
    add_candidate(other_display.x - original_display.w, other_display.y + (other_display.h - original_display.h) / 2);
    add_candidate(other_display.x + (other_display.w - original_display.w) / 2, other_display.y + other_display.h);
    add_candidate(other_display.x + (other_display.w - original_display.w) / 2, other_display.y - original_display.h);
    add_candidate(0, 0);
    add_candidate(width - original_display.w, 0);
    add_candidate(0, height - original_display.h);
    add_candidate(width - original_display.w, height - original_display.h);

    long long best_score = LLONG_MAX;
    RectI best = original;
    bool found = false;
    for (const RectI &candidate : candidates) {
        if (RectsOverlap(DisplayRect(candidate, rotate), other_display)) {
            continue;
        }
        const long long dx = static_cast<long long>(candidate.x) - original.x;
        const long long dy = static_cast<long long>(candidate.y) - original.y;
        const long long score = dx * dx + dy * dy;
        if (score < best_score) {
            best_score = score;
            best = candidate;
            found = true;
        }
    }

    if (!found) {
        return false;
    }
    *rect = best;
    return true;
}

bool PreventOverlap(RectI *rect, const RectI &other, const RectI &previous, int rotate, int width, int height)
{
    const RectI other_display = DisplayRect(other, rotate);
    if (!RectsOverlap(DisplayRect(*rect, rotate), other_display)) {
        return true;
    }

    const int dx = rect->x - previous.x;
    const int dy = rect->y - previous.y;
    const RectI rect_display = DisplayRect(*rect, rotate);

    if (std::abs(dx) >= std::abs(dy) && dx != 0) {
        rect->x = dx > 0 ? other_display.x - rect_display.w : other_display.x + other_display.w;
    } else if (dy != 0) {
        rect->y = dy > 0 ? other_display.y - rect_display.h : other_display.y + other_display.h;
    } else {
        const int left_gap = std::abs((other_display.x - rect_display.w) - rect->x);
        const int right_gap = std::abs((other_display.x + other_display.w) - rect->x);
        const int top_gap = std::abs((other_display.y - rect_display.h) - rect->y);
        const int bottom_gap = std::abs((other_display.y + other_display.h) - rect->y);
        const int best = std::min(std::min(left_gap, right_gap), std::min(top_gap, bottom_gap));
        if (best == left_gap) {
            rect->x = other_display.x - rect_display.w;
        } else if (best == right_gap) {
            rect->x = other_display.x + other_display.w;
        } else if (best == top_gap) {
            rect->y = other_display.y - rect_display.h;
        } else {
            rect->y = other_display.y + other_display.h;
        }
    }

    ClampRectForRotate(rect, rotate, width, height);
    if (RectsOverlap(DisplayRect(*rect, rotate), other_display)) {
        if (!TryMoveOutOfOverlap(rect, other, rotate, width, height)) {
            *rect = previous;
            return !RectsOverlap(DisplayRect(*rect, rotate), other_display);
        }
    }
    return true;
}

bool ResolveLayoutOverlap(Layout *layout, int width, int height)
{
    ClampRectForRotate(&layout->screen[0], layout->rotate, width, height);
    ClampRectForRotate(&layout->screen[1], layout->rotate, width, height);
    if (!RectsOverlap(DisplayRect(layout->screen[0], layout->rotate), DisplayRect(layout->screen[1], layout->rotate))) {
        return true;
    }
    return TryMoveOutOfOverlap(&layout->screen[1], layout->screen[0], layout->rotate, width, height);
}

template <typename T>
void SafeRelease(T **ptr)
{
    if (*ptr) {
        (*ptr)->Release();
        *ptr = nullptr;
    }
}

void ReleaseBackgroundTexture(AppState *state)
{
    SafeRelease(&state->bg_texture);
    state->bg_texture_path.clear();
    state->bg_texture_w = 0;
    state->bg_texture_h = 0;
}

bool LoadTextureFromFile(const std::filesystem::path &path, ID3D11ShaderResourceView **texture, int *width, int *height)
{
    IWICImagingFactory *factory = nullptr;
    IWICBitmapDecoder *decoder = nullptr;
    IWICBitmapFrameDecode *frame = nullptr;
    IWICFormatConverter *converter = nullptr;
    ID3D11Texture2D *d3d_texture = nullptr;
    bool ok = false;

    const std::wstring wide_path = Utf8ToWide(path.u8string());
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateDecoderFromFilename(wide_path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &decoder);
    }
    if (SUCCEEDED(hr)) {
        hr = decoder->GetFrame(0, &frame);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                   WICBitmapPaletteTypeCustom);
    }

    UINT image_w = 0;
    UINT image_h = 0;
    if (SUCCEEDED(hr)) {
        hr = converter->GetSize(&image_w, &image_h);
    }

    std::vector<unsigned char> pixels;
    if (SUCCEEDED(hr)) {
        pixels.resize(static_cast<size_t>(image_w) * image_h * 4);
        hr = converter->CopyPixels(nullptr, image_w * 4,
                                   static_cast<UINT>(pixels.size()), pixels.data());
    }

    if (SUCCEEDED(hr)) {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = image_w;
        desc.Height = image_h;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA subresource = {};
        subresource.pSysMem = pixels.data();
        subresource.SysMemPitch = image_w * 4;

        hr = g_pd3d_device->CreateTexture2D(&desc, &subresource, &d3d_texture);
    }
    if (SUCCEEDED(hr)) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Texture2D.MipLevels = 1;
        hr = g_pd3d_device->CreateShaderResourceView(d3d_texture, &srv_desc, texture);
    }

    if (SUCCEEDED(hr)) {
        *width = static_cast<int>(image_w);
        *height = static_cast<int>(image_h);
        ok = true;
    }

    SafeRelease(&d3d_texture);
    SafeRelease(&converter);
    SafeRelease(&frame);
    SafeRelease(&decoder);
    SafeRelease(&factory);
    return ok;
}

void EnsureBackgroundTexture(AppState *state, const Layout &layout)
{
    const std::filesystem::path bg_path = ThemeBackgroundPath(state->file_path, layout);
    const std::string path_string = bg_path.empty() ? std::string() : bg_path.u8string();
    if (path_string == state->bg_texture_path) {
        return;
    }

    ReleaseBackgroundTexture(state);
    state->bg_texture_path = path_string;
    if (path_string.empty() || !std::filesystem::exists(bg_path)) {
        return;
    }

    if (!LoadTextureFromFile(bg_path, &state->bg_texture, &state->bg_texture_w, &state->bg_texture_h)) {
        state->status = std::string(Tr(state, "Failed to load background: ", "背景加载失败：")) + path_string;
        state->bg_texture_path.clear();
    }
}

bool GenerateMaskPng(const std::filesystem::path &path, const Document &doc, const Layout &layout, std::string *error)
{
    if (doc.width <= 0 || doc.height <= 0) {
        *error = "Invalid document resolution";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        *error = "Failed to create background directory: " + ec.message();
        return false;
    }

    std::vector<unsigned char> pixels(static_cast<size_t>(doc.width) * doc.height * 4, 0);
    for (int y = 0; y < doc.height; ++y) {
        for (int x = 0; x < doc.width; ++x) {
            pixels[(static_cast<size_t>(y) * doc.width + x) * 4 + 3] = 255;
        }
    }

    for (int s = 0; s < 2; ++s) {
        RectI r = DisplayRect(layout.screen[s], layout.rotate);
        ClampRect(&r, doc.width, doc.height);
        for (int y = r.y; y < r.y + r.h; ++y) {
            for (int x = r.x; x < r.x + r.w; ++x) {
                pixels[(static_cast<size_t>(y) * doc.width + x) * 4 + 3] = 0;
            }
        }
    }

    IWICImagingFactory *factory = nullptr;
    IWICStream *stream = nullptr;
    IWICBitmapEncoder *encoder = nullptr;
    IWICBitmapFrameEncode *frame = nullptr;
    bool ok = false;

    const std::wstring wide_path = Utf8ToWide(path.u8string());
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (SUCCEEDED(hr)) {
        hr = factory->CreateStream(&stream);
    }
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(wide_path.c_str(), GENERIC_WRITE);
    }
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(&frame, nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(nullptr);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(static_cast<UINT>(doc.width), static_cast<UINT>(doc.height));
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) {
        hr = frame->SetPixelFormat(&format);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->WritePixels(static_cast<UINT>(doc.height),
                                static_cast<UINT>(doc.width * 4),
                                static_cast<UINT>(pixels.size()),
                                pixels.data());
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }

    if (SUCCEEDED(hr)) {
        ok = true;
    } else {
        *error = "Failed to write PNG";
    }

    SafeRelease(&frame);
    SafeRelease(&encoder);
    SafeRelease(&stream);
    SafeRelease(&factory);
    return ok;
}

int GenerateMissingBackgrounds(const std::string &layout_path, const Document &doc, std::string *error)
{
    int generated = 0;
    Document normalized = doc;
    EnsureDefaultBackgroundNames(&normalized);
    for (const Layout &layout : normalized.layouts) {
        if (layout.bg.empty()) {
            continue;
        }
        const std::filesystem::path bg_path = ThemeBackgroundPath(layout_path, layout);
        if (std::filesystem::exists(bg_path)) {
            continue;
        }
        if (!GenerateMaskPng(bg_path, doc, layout, error)) {
            return -1;
        }
        ++generated;
    }
    return generated;
}

int HitTest(const ImVec2 &p, const RectI &r, float handle_size)
{
    if (r.w <= 0 || r.h <= 0) {
        return 0;
    }
    const float br_x = static_cast<float>(r.x + r.w);
    const float br_y = static_cast<float>(r.y + r.h);
    if (std::fabs(p.x - br_x) <= handle_size && std::fabs(p.y - br_y) <= handle_size) {
        return 2;
    }
    return PointInside(p, r) ? 1 : 0;
}

ImVec2 ToScreen(const ImVec2 &origin, float scale, const ImVec2 &layout_pos)
{
    return ImVec2(origin.x + layout_pos.x * scale, origin.y + layout_pos.y * scale);
}

void DrawScreenRect(ImDrawList *draw, const ImVec2 &origin, float scale, const RectI &rect,
                    const char *label, ImU32 fill, ImU32 border, bool selected)
{
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const ImVec2 p0 = ToScreen(origin, scale, ImVec2(static_cast<float>(rect.x), static_cast<float>(rect.y)));
    const ImVec2 p1 = ToScreen(origin, scale, ImVec2(static_cast<float>(rect.x + rect.w), static_cast<float>(rect.y + rect.h)));
    draw->AddRectFilled(p0, p1, fill);
    draw->AddRect(p0, p1, selected ? IM_COL32(255, 255, 255, 255) : border, 0.0f, 0, selected ? 3.0f : 2.0f);
    draw->AddText(ImVec2(p0.x + 6.0f, p0.y + 6.0f), IM_COL32(255, 255, 255, 255), label);
    draw->AddRectFilled(ImVec2(p1.x - 8.0f, p1.y - 8.0f), p1, IM_COL32(255, 255, 255, 220));
}

void DrawCanvas(AppState *state)
{
    Document &doc = state->doc;
    Layout &layout = doc.layouts[state->current_layout];
    EnsureBackgroundTexture(state, layout);

    ImGui::BeginChild("canvas_region", ImVec2(0, 0), true,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 start = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.x = std::max(avail.x, 64.0f);
    avail.y = std::max(avail.y, 64.0f);

    ImGui::InvisibleButton("canvas_input", avail, ImGuiButtonFlags_MouseButtonLeft);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList *draw = ImGui::GetWindowDrawList();

    const float scale_x = (avail.x - 24.0f) / static_cast<float>(std::max(1, doc.width));
    const float scale_y = (avail.y - 24.0f) / static_cast<float>(std::max(1, doc.height));
    const float scale = std::max(0.05f, std::min(scale_x, scale_y));
    const ImVec2 canvas_size(static_cast<float>(doc.width) * scale, static_cast<float>(doc.height) * scale);
    const ImVec2 origin(start.x + (avail.x - canvas_size.x) * 0.5f,
                        start.y + (avail.y - canvas_size.y) * 0.5f);
    const ImVec2 canvas_max(origin.x + canvas_size.x, origin.y + canvas_size.y);

    draw->AddRectFilled(origin, canvas_max, IM_COL32(28, 31, 36, 255));
    if (state->bg_texture) {
        draw->AddImage((ImTextureID)(intptr_t)state->bg_texture, origin, canvas_max);
    }
    draw->AddRect(origin, canvas_max, IM_COL32(180, 180, 180, 255));

    for (int x = 0; x <= doc.width; x += std::max(64, doc.width / 12)) {
        const float sx = origin.x + static_cast<float>(x) * scale;
        draw->AddLine(ImVec2(sx, origin.y), ImVec2(sx, canvas_max.y), IM_COL32(255, 255, 255, 24));
    }
    for (int y = 0; y <= doc.height; y += std::max(64, doc.height / 8)) {
        const float sy = origin.y + static_cast<float>(y) * scale;
        draw->AddLine(ImVec2(origin.x, sy), ImVec2(canvas_max.x, sy), IM_COL32(255, 255, 255, 24));
    }

    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const ImVec2 layout_mouse((mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale);
        const float handle = 10.0f / scale;
        state->interaction = {};
        for (int s = 1; s >= 0; --s) {
            const int mode = HitTest(layout_mouse, DisplayRect(layout.screen[s], layout.rotate), handle);
            if (mode != 0) {
                state->interaction.screen = s;
                state->interaction.mode = mode;
                state->interaction.start_mouse = layout_mouse;
                state->interaction.start_rect = layout.screen[s];
                state->selected_screen = s;
                break;
            }
        }
    }

    if (state->interaction.screen >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const ImVec2 layout_mouse((mouse.x - origin.x) / scale, (mouse.y - origin.y) / scale);
            const int dx = static_cast<int>(std::lround(layout_mouse.x - state->interaction.start_mouse.x));
            const int dy = static_cast<int>(std::lround(layout_mouse.y - state->interaction.start_mouse.y));
            RectI next = state->interaction.start_rect;

            if (state->interaction.mode == 1) {
                next.x += dx;
                next.y += dy;
            } else if (state->interaction.mode == 2) {
                const RectI start_display = DisplayRect(state->interaction.start_rect, layout.rotate);
                if (state->lock_aspect) {
                    SetLockedAspectFromDisplayWidth(&next,
                                                    layout.rotate,
                                                    start_display.w + dx,
                                                    doc.width,
                                                    doc.height);
                } else if (IsQuarterRotated(layout.rotate)) {
                    const int display_w = std::max(1, start_display.w + dx);
                    const int display_h = std::max(1, start_display.h + dy);
                    next.w = display_h;
                    next.h = display_w;
                } else {
                    next.w = std::max(1, next.w + dx);
                    next.h = std::max(1, next.h + dy);
                }
            }

            if (state->lock_aspect && state->interaction.mode == 2) {
                ClampLockedAspectToCanvas(&next, layout.rotate, doc.width, doc.height);
            } else {
                ClampRectForRotate(&next, layout.rotate, doc.width, doc.height);
            }
            if (!state->allow_overlap) {
                PreventOverlap(&next,
                               layout.screen[1 - state->interaction.screen],
                               state->interaction.start_rect,
                               layout.rotate,
                               doc.width,
                               doc.height);
            }
            layout.screen[state->interaction.screen] = next;
        } else {
            state->interaction = {};
        }
    }

    DrawScreenRect(draw, origin, scale, DisplayRect(layout.screen[0], layout.rotate), "screen0",
                   IM_COL32(61, 115, 210, 180), IM_COL32(88, 162, 255, 255),
                   state->selected_screen == 0);
    DrawScreenRect(draw, origin, scale, DisplayRect(layout.screen[1], layout.rotate), "screen1",
                   IM_COL32(71, 168, 94, 180), IM_COL32(107, 224, 132, 255),
                   state->selected_screen == 1);

    char overlay[192];
    std::snprintf(overlay, sizeof(overlay), Tr(state,
                  "%dx%d  |  rotate %d  |  drag to move, bottom-right handle to resize",
                  "%dx%d  |  旋转 %d  |  拖拽移动，拖右下角缩放"),
                  doc.width, doc.height, layout.rotate);
    draw->AddText(ImVec2(origin.x + 8.0f, canvas_max.y - 22.0f), IM_COL32(230, 230, 230, 255), overlay);

    ImGui::EndChild();
}

void DrawLayoutList(AppState *state)
{
    Document &doc = state->doc;

    ImGui::TextUnformatted(Tr(state, "Document", "全局设置"));
    const char *languages[] = {"English", "简体中文"};
    int language_index = state->language == UiLanguage::Chinese ? 1 : 0;
    if (ImGui::Combo(Tr(state, "Language", "语言"), &language_index, languages, IM_ARRAYSIZE(languages))) {
        state->language = language_index == 1 ? UiLanguage::Chinese : UiLanguage::English;
        state->status = Tr(state, "Language changed", "语言已切换");
    }
    InputTextString(Tr(state, "Name", "名称"), &doc.name);
    if (ImGui::InputInt(Tr(state, "Width", "宽度"), &doc.width)) {
        doc.width = std::max(1, doc.width);
    }
    if (ImGui::InputInt(Tr(state, "Height", "高度"), &doc.height)) {
        doc.height = std::max(1, doc.height);
    }

    ImGui::Spacing();
    ImGui::Text(Tr(state, "Layouts (%d/%d)", "布局列表 (%d/%d)"), static_cast<int>(doc.layouts.size()), kMaxLayouts);
    ImGui::Separator();

    for (size_t i = 0; i < doc.layouts.size(); ++i) {
        char label[256];
        std::snprintf(label, sizeof(label), "%d: %s", static_cast<int>(i), doc.layouts[i].name.c_str());
        if (ImGui::Selectable(label, state->current_layout == static_cast<int>(i))) {
            state->current_layout = static_cast<int>(i);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(state, "Actions", "操作"));
    ImGui::BeginDisabled(doc.layouts.size() >= static_cast<size_t>(kMaxLayouts));
    if (ImGui::Button(Tr(state, "Add Layout", "新增布局"), ImVec2(-FLT_MIN, 0))) {
        Layout layout = MakeDefaultLayout(doc, "New Layout");
        layout.index = static_cast<int>(doc.layouts.size());
        doc.layouts.push_back(layout);
        state->current_layout = static_cast<int>(doc.layouts.size()) - 1;
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(doc.layouts.empty() || doc.layouts.size() >= static_cast<size_t>(kMaxLayouts));
    if (ImGui::Button(Tr(state, "Duplicate Selected", "复制选中布局"), ImVec2(-FLT_MIN, 0))) {
        Layout layout = doc.layouts[state->current_layout];
        layout.name += " Copy";
        doc.layouts.push_back(layout);
        NormalizeIndices(&doc);
        state->current_layout = static_cast<int>(doc.layouts.size()) - 1;
    }
    ImGui::EndDisabled();

    ImGui::BeginDisabled(doc.layouts.size() <= 1);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.12f, 0.12f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.18f, 0.18f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
    if (ImGui::Button(Tr(state, "Delete Selected", "删除选中布局"), ImVec2(-FLT_MIN, 0))) {
        doc.layouts.erase(doc.layouts.begin() + state->current_layout);
        state->current_layout = std::min(state->current_layout, static_cast<int>(doc.layouts.size()) - 1);
        NormalizeIndices(&doc);
    }
    ImGui::PopStyleColor(3);
    ImGui::EndDisabled();
}

void DrawInspector(AppState *state)
{
    Document &doc = state->doc;
    Layout &layout = doc.layouts[state->current_layout];

    ImGui::TextUnformatted(Tr(state, "Current Layout", "当前布局"));
    InputTextString(Tr(state, "Layout Name", "布局名称"), &layout.name);
    if (InputTextString(Tr(state, "Background", "背景图"), &layout.bg)) {
        NormalizeBackgroundName(&layout);
        ReleaseBackgroundTexture(state);
    }

    const char *types_en[] = {"Dual Screen", "Transparent Overlay", "Single Screen"};
    const char *types_zh[] = {"双屏", "透明叠加", "单屏"};
    const char **types = state->language == UiLanguage::Chinese ? types_zh : types_en;
    int type_index = 0;
    if (layout.type == 1) {
        type_index = 1;
    } else if (layout.type == 4) {
        type_index = 2;
    }
    if (ImGui::Combo(Tr(state, "Type", "类型"), &type_index, types, 3)) {
        layout.type = type_index == 0 ? 0 : type_index == 1 ? 1 : 4;
        if (layout.type == 1) {
            state->allow_overlap = true;
            layout.screen[1].x = 0;
            layout.screen[1].y = 0;
        }
    }

    const char *rotations[] = {"0", "90", "180", "270"};
    int rotate_index = 0;
    if (layout.rotate == 90) {
        rotate_index = 1;
    } else if (layout.rotate == 180) {
        rotate_index = 2;
    } else if (layout.rotate == 270) {
        rotate_index = 3;
    }
    if (ImGui::Combo(Tr(state, "Rotate", "旋转"), &rotate_index, rotations, IM_ARRAYSIZE(rotations))) {
        layout.rotate = rotate_index == 0 ? 0 : rotate_index == 1 ? 90 : rotate_index == 2 ? 180 : 270;
        ClampRectForRotate(&layout.screen[0], layout.rotate, doc.width, doc.height);
        ClampRectForRotate(&layout.screen[1], layout.rotate, doc.width, doc.height);
        if (!state->allow_overlap) {
            ResolveLayoutOverlap(&layout, doc.width, doc.height);
        }
    }

    ImGui::Checkbox(Tr(state, "Lock 4:3 resize", "锁定 4:3 缩放"), &state->lock_aspect);
    if (ImGui::Checkbox(Tr(state, "Allow screen overlap", "允许双屏重叠"), &state->allow_overlap) && !state->allow_overlap) {
        if (!ResolveLayoutOverlap(&layout, doc.width, doc.height)) {
            state->allow_overlap = true;
            state->status = Tr(state,
                                "Cannot disable overlap: the current screens do not fit without overlapping.",
                                "无法禁用重叠：当前两个屏幕无法在画布内无重叠放下。");
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(Tr(state, "Templates", "模板"));
    if (ImGui::BeginTable("template_buttons", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr(state, "Side by Side", "左右并排"), ImVec2(-FLT_MIN, 0))) {
            ApplyTemplate(&doc, &layout, 0);
            if (!state->allow_overlap) {
                ResolveLayoutOverlap(&layout, doc.width, doc.height);
            }
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr(state, "Vertical Stack", "上下排列"), ImVec2(-FLT_MIN, 0))) {
            ApplyTemplate(&doc, &layout, 1);
            if (!state->allow_overlap) {
                ResolveLayoutOverlap(&layout, doc.width, doc.height);
            }
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr(state, "Top Full", "上屏最大"), ImVec2(-FLT_MIN, 0))) {
            ApplyTemplate(&doc, &layout, 2);
            if (!state->allow_overlap) {
                ResolveLayoutOverlap(&layout, doc.width, doc.height);
            }
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr(state, "Transparent", "透明叠加"), ImVec2(-FLT_MIN, 0))) {
            state->allow_overlap = true;
            ApplyTemplate(&doc, &layout, 3);
        }
        ImGui::TableNextColumn();
        if (ImGui::Button(Tr(state, "Single Screen", "单屏"), ImVec2(-FLT_MIN, 0))) {
            ApplyTemplate(&doc, &layout, 4);
            if (!state->allow_overlap) {
                ResolveLayoutOverlap(&layout, doc.width, doc.height);
            }
        }
        ImGui::EndTable();
    }

    ImGui::Separator();
    for (int s = 0; s < 2; ++s) {
        ImGui::PushID(s);
        if (ImGui::Selectable(s == 0 ? "screen0" : "screen1", state->selected_screen == s)) {
            state->selected_screen = s;
        }
        RectI &r = layout.screen[s];
        const RectI previous = r;
        const bool transparent_bottom = layout.type == 1 && s == 1;
        if (transparent_bottom) {
            r.x = 0;
            r.y = 0;
            ImGui::BeginDisabled();
        }
        ImGui::InputInt("x", &r.x);
        ImGui::InputInt("y", &r.y);
        if (transparent_bottom) {
            ImGui::EndDisabled();
            r.x = 0;
            r.y = 0;
            ImGui::TextDisabled("%s", Tr(state,
                                "Transparent mode uses runtime position for screen1.",
                                "透明模式下 screen1 位置由运行时设置决定。"));
        }
        if (ImGui::InputInt("w", &r.w) && state->lock_aspect) {
            KeepAspectFromWidth(&r);
        }
        if (ImGui::InputInt("h", &r.h) && state->lock_aspect) {
            r.w = std::max(0, r.h * kNdsAspectW / kNdsAspectH);
        }
        if (state->lock_aspect) {
            ClampLockedAspectToCanvas(&r, layout.rotate, doc.width, doc.height);
        } else {
            ClampRectForRotate(&r, layout.rotate, doc.width, doc.height);
        }
        if (!state->allow_overlap) {
            PreventOverlap(&r, layout.screen[1 - s], previous, layout.rotate, doc.width, doc.height);
        }
        ImGui::Spacing();
        ImGui::PopID();
    }
}

void DrawMainUi(AppState *state)
{
    EnsureDefaultDocument(&state->doc);
    state->current_layout = std::max(0, std::min(state->current_layout,
                                                 static_cast<int>(state->doc.layouts.size()) - 1));

    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    const ImGuiWindowFlags window_flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("Drastic Layout Editor", nullptr, window_flags);
    UpdateWindowTitle(state);

    Layout &active_layout = state->doc.layouts[state->current_layout];
    if (active_layout.type == 1) {
        state->allow_overlap = true;
        active_layout.screen[1].x = 0;
        active_layout.screen[1].y = 0;
    }

    ImGui::InputText(Tr(state, "layout.json path", "layout.json 路径"), state->file_path, sizeof(state->file_path), ImGuiInputTextFlags_ReadOnly);
    ImGui::SameLine();
    if (ImGui::Button(Tr(state, "Load", "加载"))) {
        std::string error;
        if (BrowseLayoutJson(state->file_path, sizeof(state->file_path), false)) {
            if (LoadLayoutFile(state->file_path, &state->doc, &error)) {
                state->current_layout = 0;
                ReleaseBackgroundTexture(state);
                if (!state->allow_overlap) {
                    ResolveLayoutOverlap(&state->doc.layouts[state->current_layout],
                                         state->doc.width,
                                         state->doc.height);
                }
                state->status = std::string(Tr(state, "Loaded ", "已加载 ")) + state->file_path;
            } else {
                state->status = std::string(Tr(state, "Load failed: ", "加载失败：")) + error;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(Tr(state, "Save", "保存"))) {
        std::string error;
        if (state->file_path[0] == '\0') {
            std::snprintf(state->file_path, sizeof(state->file_path), "%s",
                          DefaultSaveLayoutPath(state->doc.width, state->doc.height).c_str());
        }
            EnsureDefaultBackgroundNames(&state->doc);
            if (SaveLayoutFile(state->file_path, state->doc, &error)) {
                const int generated = GenerateMissingBackgrounds(state->file_path, state->doc, &error);
                if (generated < 0) {
                    state->status = std::string(Tr(state,
                                         "Saved layout, but background generation failed: ",
                                         "布局已保存，但背景生成失败：")) + error;
                } else {
                    ReleaseBackgroundTexture(state);
                    state->status = std::string(Tr(state, "Saved ", "已保存 ")) + state->file_path;
                    if (generated > 0) {
                        state->status += std::string(Tr(state,
                                           " and generated ",
                                           "，并生成了 ")) + std::to_string(generated) +
                                           Tr(state, " missing background PNG(s)", " 个缺失的背景 PNG");
                    }
                }
            } else {
                state->status = std::string(Tr(state, "Save failed: ", "保存失败：")) + error;
            }
    }
    ImGui::TextWrapped("%s", state->status.c_str());

    ImGui::Separator();

    const float left_width = 260.0f;
    const float right_width = 340.0f;
    ImGui::BeginChild("left_panel", ImVec2(left_width, 0), true);
    DrawLayoutList(state);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("canvas_panel", ImVec2(-right_width - ImGui::GetStyle().ItemSpacing.x, 0), false);
    DrawCanvas(state);
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("right_panel", ImVec2(right_width, 0), true);
    DrawInspector(state);
    ImGui::EndChild();

    ImGui::End();
}

bool CreateRenderTarget()
{
    ID3D11Texture2D *back_buffer = nullptr;
    if (FAILED(g_swap_chain->GetBuffer(0, IID_PPV_ARGS(&back_buffer)))) {
        return false;
    }
    if (FAILED(g_pd3d_device->CreateRenderTargetView(back_buffer, nullptr, &g_main_render_target_view))) {
        back_buffer->Release();
        return false;
    }
    back_buffer->Release();
    return true;
}

void CleanupRenderTarget()
{
    if (g_main_render_target_view) {
        g_main_render_target_view->Release();
        g_main_render_target_view = nullptr;
    }
}

bool CreateDeviceD3D(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT create_device_flags = 0;
    D3D_FEATURE_LEVEL feature_level = {};
    const D3D_FEATURE_LEVEL feature_level_array[2] = {
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_0,
    };

    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr,
                                                D3D_DRIVER_TYPE_HARDWARE,
                                                nullptr,
                                                create_device_flags,
                                                feature_level_array,
                                                2,
                                                D3D11_SDK_VERSION,
                                                &sd,
                                                &g_swap_chain,
                                                &g_pd3d_device,
                                                &feature_level,
                                                &g_pd3d_device_context);
    if (res == DXGI_ERROR_UNSUPPORTED) {
        res = D3D11CreateDeviceAndSwapChain(nullptr,
                                            D3D_DRIVER_TYPE_WARP,
                                            nullptr,
                                            create_device_flags,
                                            feature_level_array,
                                            2,
                                            D3D11_SDK_VERSION,
                                            &sd,
                                            &g_swap_chain,
                                            &g_pd3d_device,
                                            &feature_level,
                                            &g_pd3d_device_context);
    }
    if (res != S_OK) {
        return false;
    }

    return CreateRenderTarget();
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_swap_chain) {
        g_swap_chain->Release();
        g_swap_chain = nullptr;
    }
    if (g_pd3d_device_context) {
        g_pd3d_device_context->Release();
        g_pd3d_device_context = nullptr;
    }
    if (g_pd3d_device) {
        g_pd3d_device->Release();
        g_pd3d_device = nullptr;
    }
}

LRESULT WINAPI WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return true;
    }

    switch (msg) {
    case WM_SIZE:
        if (wparam != SIZE_MINIMIZED && g_pd3d_device != nullptr) {
            CleanupRenderTarget();
            g_swap_chain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lparam)), static_cast<UINT>(HIWORD(lparam)), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wparam & 0xfff0) == SC_KEYMENU) {
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}

} // namespace

int APIENTRY WinMain(HINSTANCE hinstance, HINSTANCE, LPSTR, int)
{
    const HRESULT coinit_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hinstance;
    wc.lpszClassName = _T("DrasticLayoutEditor");
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindow(wc.lpszClassName,
                             _T("Drastic Layout Editor"),
                             WS_OVERLAPPEDWINDOW,
                             100,
                             100,
                             1280,
                             800,
                             nullptr,
                             nullptr,
                             wc.hInstance,
                             nullptr);
    if (!hwnd) {
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        if (SUCCEEDED(coinit_hr)) {
            CoUninitialize();
        }
        return 1;
    }
    g_main_hwnd = hwnd;

    HICON app_icon = LoadIcon(hinstance, "IDI_APP_ICON");
    if (app_icon) {
        SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app_icon));
        SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(app_icon));
    }

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        DestroyWindow(hwnd);
        UnregisterClass(wc.lpszClassName, wc.hInstance);
        if (SUCCEEDED(coinit_hr)) {
            CoUninitialize();
        }
        return 1;
    }

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    LoadUiFonts(&io);

    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3d_device, g_pd3d_device_context);

    AppState state;
    EnsureDefaultDocument(&state.doc);
    state.status = Tr(&state, "Ready", "就绪");

    bool done = false;
    while (!done) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) {
                done = true;
            }
        }
        if (done) {
            break;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawMainUi(&state);

        ImGui::Render();

        const float clear_color[4] = {0.08f, 0.09f, 0.10f, 1.0f};
        g_pd3d_device_context->OMSetRenderTargets(1, &g_main_render_target_view, nullptr);
        g_pd3d_device_context->ClearRenderTargetView(g_main_render_target_view, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_swap_chain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    ReleaseBackgroundTexture(&state);
    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(wc.lpszClassName, wc.hInstance);

    if (SUCCEEDED(coinit_hr)) {
        CoUninitialize();
    }

    return 0;
}
