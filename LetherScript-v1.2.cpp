#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <cctype>
#include <cmath>
#include <windows.h>
#include <shellapi.h>
#include <cstdlib>
#include <random>
#include <algorithm>
#include <vector>
#include <chrono>
#include <filesystem>
#include <sstream>
#include <locale>
#include <ctime>
#include <cstdint>

using namespace std;

// ============================================================
//  全局标志
// ============================================================
bool traceLogEnabled = false;
bool debugMessageEnabled = false;
bool fullscreen_enabled = false;
ofstream traceLogFile;

// 全局输出重定向
ofstream outputRedirectFile;
bool outputRedirectEnabled = false;

// 用户定义的文件流 (filestream 变量名 → fstream)
map<string, fstream*> fileStreamMap;

// 当前脚本文件所在目录（用于 using 相对路径）
string currentScriptDir;

// 获取可执行文件所在目录
string get_exe_dir() {
    char buf[MAX_PATH];
    GetModuleFileNameA(NULL, buf, MAX_PATH);
    string exe(buf);
    size_t p = exe.find_last_of("/\\");
    return (p == string::npos) ? "" : exe.substr(0, p + 1);
}

// 函数定义
struct FuncDef {
    vector<string> param_names;
    vector<string> body_lines; // 完整的函数体代码行
};
map<string, FuncDef> user_functions;

// ============================================================
//  值类型系统
// ============================================================
enum class ValType { Int32, Int64, Char, String, AnsiString, Array, FileStream };

struct Value {
    ValType type;
    int64_t num_val;      // Int32 / Int64
    char    char_val;     // Char
    string  str_val;      // String / AnsiString
    vector<Value> arr_val; // Array

    Value() : type(ValType::Int32), num_val(0), char_val(0) {}

    static Value make_int32(int32_t v) {
        Value val; val.type = ValType::Int32; val.num_val = v; return val;
    }
    static Value make_int64(int64_t v) {
        Value val; val.type = ValType::Int64; val.num_val = v; return val;
    }
    static Value make_char(char c) {
        Value val; val.type = ValType::Char; val.char_val = c; return val;
    }
    static Value make_string(const string& s) {
        Value val; val.type = ValType::String; val.str_val = s; return val;
    }
    static Value make_ansi_string(const string& s) {
        Value val; val.type = ValType::AnsiString; val.str_val = s; return val;
    }
    static Value make_array(const vector<Value>& a) {
        Value val; val.type = ValType::Array; val.arr_val = a; return val;
    }
    static Value make_filestream() {
        Value val; val.type = ValType::FileStream; return val;
    }

    string toString() const {
        switch (type) {
            case ValType::Int32:  return to_string((int32_t)num_val);
            case ValType::Int64:  return to_string(num_val);
            case ValType::Char:   return string(1, char_val);
            case ValType::String:
            case ValType::AnsiString: return str_val;
            case ValType::Array: {
                string r = "[";
                for (size_t i = 0; i < arr_val.size(); i++) {
                    if (i > 0) r += ", ";
                    r += arr_val[i].toString();
                }
                return r + "]";
            }
        }
        return "";
    }

    int64_t toInt64() const {
        if (type == ValType::Int32 || type == ValType::Int64) return num_val;
        if (type == ValType::Char)  return (int64_t)char_val;
        return 0;
    }
};

map<string, Value> variables;

// 返回值传递 (用于 return 语句)
bool function_returned = false;
Value function_return_value;

// ============================================================
//  前向声明
// ============================================================
void log_event(const string& msg);
string get_current_timestamp();
string get_current_date_ymd();
WORD hex_char_to_color(char c);
void print_with_color_codes(const string& line);
void python_error(const string& msg);
string trim(const string& s);
bool is_valid_varname(const string& name);
string parse_string_content(const string& s, size_t& pos);
Value parse_literal(const string& s, size_t& pos);
vector<string> extract_paren_args(const string& s);
string extract_paren_content(const string& s, size_t after_kw);
int64_t eval_expression(const string& expr);
vector<Value> parse_array_literal(const string& s, size_t expected_size);
string process_format_string(const string& fmt);
bool evaluate_condition(const string& cond);
Value evaluate_value(const string& expr);
Value call_user_function(const string& fname, const string& args_str);
bool execute_statement(const string& stmt);
bool execute_program_lines(const vector<string>& lines, size_t start_idx);
size_t process_if_block(const vector<string>& lines, size_t line_idx, bool& continue_flag);
void run_script_file(const string& path);
void repl_mode();

// ============================================================
//  错误处理
// ============================================================
void python_error(const string& msg) {
    cerr << "Error: " << msg << endl;
    HWND taskbar = FindWindow("Shell_TrayWnd", NULL);
    if (taskbar) ShowWindow(taskbar, SW_SHOW);
    throw runtime_error(msg);
}

// ============================================================
//  时间函数
// ============================================================
string get_current_timestamp() {
    using namespace chrono;
    auto now = system_clock::now();
    auto seconds = duration_cast<chrono::seconds>(now.time_since_epoch()).count();
    return to_string(seconds);
}

string get_current_date_ymd() {
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    char buffer[11];
    strftime(buffer, sizeof(buffer), "%Y.%m.%d", timeinfo);
    return string(buffer);
}

// ============================================================
//  颜色控制台输出
// ============================================================
WORD hex_char_to_color(char c) {
    c = toupper(static_cast<unsigned char>(c));
    switch (c) {
        case '0': return 0;
        case '1': return FOREGROUND_BLUE;
        case '2': return FOREGROUND_GREEN;
        case '3': return FOREGROUND_GREEN | FOREGROUND_BLUE;
        case '4': return FOREGROUND_RED;
        case '5': return FOREGROUND_RED | FOREGROUND_BLUE;
        case '6': return FOREGROUND_RED | FOREGROUND_GREEN;
        case '7': return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
        case '8': return FOREGROUND_INTENSITY;
        case '9': return FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'A': return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case 'B': return FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'C': return FOREGROUND_RED | FOREGROUND_INTENSITY;
        case 'D': return FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        case 'E': return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
        case 'F': return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
        default: return 7;
    }
}

void print_with_color_codes(const string& line) {
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    WORD defaultAttributes = 7;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        defaultAttributes = csbi.wAttributes;
    }
    size_t pos = 0;
    while (pos < line.size()) {
        if (line[pos] == '%' && pos + 3 < line.size()) {
            char color_char = line[pos + 1];
            if (((color_char >= '0' && color_char <= '9') ||
                 (color_char >= 'A' && color_char <= 'F') ||
                 (color_char >= 'a' && color_char <= 'f')) &&
                line[pos + 2] == '{') {
                size_t brace_end = line.find("}%", pos + 3);
                if (brace_end != string::npos) {
                    string text = line.substr(pos + 3, brace_end - (pos + 3));
                    WORD color = hex_char_to_color(color_char);
                    SetConsoleTextAttribute(hConsole, color);
                    DWORD written;
                    WriteConsoleA(hConsole, text.c_str(), (DWORD)text.size(), &written, NULL);
                    SetConsoleTextAttribute(hConsole, defaultAttributes);
                    pos = brace_end + 2;
                    continue;
                }
            }
        }
        DWORD written;
        WriteConsoleA(hConsole, &line[pos], 1, &written, NULL);
        pos++;
    }
}

// ============================================================
//  日志
// ============================================================
void log_event(const string& msg) {
    if (!traceLogEnabled) return;
    if (!traceLogFile.is_open()) return;
    traceLogFile << "[" << get_current_timestamp() << "] " << msg << endl;
}

// ============================================================
//  工具函数
// ============================================================
string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

string trim_left(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";
    return s.substr(start);
}

bool is_valid_varname(const string& name) {
    if (name.empty()) return false;
    if (name == "TIME") return false;
    char first = name[0];
    if ((first >= '0' && first <= '9') || first == '_') return false;
    for (char c : name) {
        bool valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= '0' && c <= '9') || c == '_';
        if (!valid) return false;
    }
    return true;
}

// ============================================================
//  解析字符串字面量（含转义）
// ============================================================
string parse_string_content(const string& s, size_t& pos) {
    if (pos >= s.size() || s[pos] != '"') return "";
    pos++; // skip "
    string result;
    while (pos < s.size()) {
        char c = s[pos];
        if (c == '"') {
            pos++; // skip "
            return result;
        }
        if (c == '\\' && pos + 1 < s.size()) {
            char n = s[pos + 1];
            switch (n) {
                case 'n': result += '\n'; break;
                case 't': result += '\t'; break;
                case 'r': result += '\r'; break;
                case '\\': result += '\\'; break;
                case '"': result += '"';  break;
                case '0': result += '\0'; break;
                default:  result += '\\'; result += n; break;  // 保留未知转义如 \h → \h
            }
            pos += 2;
            continue;
        }
        result += c;
        pos++;
    }
    python_error("unterminated string literal");

    return "";
}

// ============================================================
//  解析值字面量
// ============================================================
Value parse_literal(const string& s, size_t& pos) {
    // 跳过空白
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos >= s.size()) python_error("expected a value but reached end of input");

    // ANSI 字符串: ansi"..."
    if (pos + 4 <= s.size() && s.substr(pos, 4) == "ansi") {
        pos += 4;
        string content = parse_string_content(s, pos);
        return Value::make_ansi_string(content);
    }

    // 双引号字符串: "..."
    if (s[pos] == '"') {
        string content = parse_string_content(s, pos);
        return Value::make_string(content);
    }

    // 单引号字符: 'x'
    if (s[pos] == '\'') {
        pos++; // skip '
        if (pos >= s.size()) python_error("empty character literal");
        char ch = s[pos++];
        if (pos >= s.size() || s[pos] != '\'')
            python_error("character literal missing closing single-quote");
        pos++; // skip '
        return Value::make_char(ch);
    }

    // 数字：可选负号 + 数字 + 可选的 i 后缀
    bool neg = false;
    if (s[pos] == '-') { neg = true; pos++; }
    if (pos >= s.size() || !isdigit((unsigned char)s[pos]))
        python_error("expected a digit");

    string digits;
    while (pos < s.size() && isdigit((unsigned char)s[pos])) {
        digits += s[pos]; pos++;
    }

    if (neg) digits = "-" + digits;

    // i 后缀 → int64
    if (pos < s.size() && s[pos] == 'i') {
        pos++; // skip i
        int64_t v = stoll(digits);
        return Value::make_int64(v);
    }

    // 否则 int32
    int32_t v = stol(digits);
    return Value::make_int32(v);
}

// ============================================================
//  解析数组字面量 (v1, v2, ...)
// ============================================================
vector<Value> parse_array_literal(const string& s, size_t expected_size) {
    string ts = trim(s);
    if (ts.size() < 2 || ts.front() != '(' || ts.back() != ')')
        python_error("array initializer must be enclosed in (...), got: " + s);
    string inner = trim(ts.substr(1, ts.size() - 2));
    vector<Value> result;
    size_t pos = 0;
    while (pos < inner.size()) {
        while (pos < inner.size() && (inner[pos] == ',' || inner[pos] == ' ' || inner[pos] == '\t')) pos++;
        if (pos >= inner.size()) break;
        size_t start = pos;
        while (pos < inner.size() && inner[pos] != ',') pos++;
        string val_str = trim(inner.substr(start, pos - start));
        if (!val_str.empty()) {
            size_t vp = 0;
            bool parsed = false;
            try {
                Value v = parse_literal(val_str, vp);
                if (vp == val_str.size()) { result.push_back(v); parsed = true; }
            } catch (...) {}
            if (!parsed) {
                int64_t ival = eval_expression(val_str);
                result.push_back(Value::make_int32((int32_t)ival));
            }
        }
    }
    // 单值展开：x[3] = (0) → 全部设为 0
    if (result.size() == 1) {
        Value single = result[0];
        result.assign(expected_size, single);
    }
    if (result.size() != expected_size)
        python_error("array initializer: expected " + to_string(expected_size) +
                     " elements but got " + to_string(result.size()));
    return result;
}

// ============================================================
//  提取标识符
// ============================================================
string parse_identifier(const string& s, size_t& pos) {
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t')) pos++;
    if (pos >= s.size()) return "";
    if (!isalpha((unsigned char)s[pos]) && s[pos] != '_') return "";
    size_t start = pos;
    while (pos < s.size() && (isalnum((unsigned char)s[pos]) || s[pos] == '_'))
        pos++;
    return s.substr(start, pos - start);
}

// ============================================================
//  表达式求值（返回 string 形式，用于浮点数场景）
// ============================================================
static string eval_expression_str(const string& expr) {
    // 如果是纯浮点数字面量，直接返回
    string t = trim(expr);
    bool has_dot = false;
    for (char c : t) if (c == '.') has_dot = true;
    bool all_digits_or_dot = true;
    for (char c : t) if (!isdigit((unsigned char)c) && c != '.' && c != '-') { all_digits_or_dot = false; break; }
    if (has_dot && all_digits_or_dot && !t.empty()) return t;
    // 否则走整数表达式求值
    return to_string(eval_expression(expr));
}

// ============================================================
//  表达式求值（int64，支持 + - * / ^ 和括号）
// ============================================================
// 内置数学函数名称列表
static bool is_math_func(const string& n) {
    static const char* names[] = {"sqrt","ceil","floor","round","abs","sin","cos","tan",
                                   "log","log10","pow","min","max","fmod", nullptr};
    for (int i = 0; names[i]; i++) if (n == names[i]) return true;
    return false;
}
int64_t eval_expression(const string& expr) {
    string e;
    for (char c : expr) if (c != ' ' && c != '\t') e.push_back(c);
    if (e.empty()) python_error("empty expression");

    // 函数调用检测（在括号处理之前！）
    {
        size_t oparen = e.find('(');
        if (oparen != string::npos) {
            string fname = e.substr(0, oparen);
            if (is_math_func(fname) || user_functions.find(fname) != user_functions.end()) {
                string call_args = extract_paren_content(e, (int)oparen);
                if (is_math_func(fname)) {
                    double arg1 = 0, arg2 = 0;
                    if (fname == "pow" || fname == "min" || fname == "max" || fname == "fmod") {
                        vector<string> ma = extract_paren_args("(" + call_args + ")");
                        arg1 = stod(eval_expression_str(ma[0]));
                        arg2 = stod(eval_expression_str(ma.size() > 1 ? ma[1] : "0"));
                    } else {
                        arg1 = stod(eval_expression_str(call_args));
                    }
                    if (fname == "sqrt")  return (int64_t)sqrt(arg1);
                    if (fname == "ceil")  return (int64_t)ceil(arg1);
                    if (fname == "floor") return (int64_t)floor(arg1);
                    if (fname == "round") return (int64_t)round(arg1);
                    if (fname == "abs")   return (int64_t)fabs(arg1);
                    if (fname == "sin")   return (int64_t)sin(arg1);
                    if (fname == "cos")   return (int64_t)cos(arg1);
                    if (fname == "tan")   return (int64_t)tan(arg1);
                    if (fname == "log")   return (int64_t)log(arg1);
                    if (fname == "log10") return (int64_t)log10(arg1);
                    if (fname == "pow")   return (int64_t)pow(arg1, arg2);
                    if (fname == "min")   return (int64_t)(arg1 < arg2 ? arg1 : arg2);
                    if (fname == "max")   return (int64_t)(arg1 > arg2 ? arg1 : arg2);
                    if (fname == "fmod")  return (int64_t)fmod(arg1, arg2);
                }
                if (user_functions.find(fname) != user_functions.end())
                    return call_user_function(fname, call_args).toInt64();
            }
        }
    }

    // 处理括号（现在括号内的内容不会再包含函数名）
    while (true) {
        size_t cp = e.find(')');
        if (cp == string::npos) break;
        size_t op = e.rfind('(', cp);
        if (op == string::npos) python_error("mismatched parentheses in expression");
        string inner = e.substr(op + 1, cp - op - 1);
        int64_t inner_val = eval_expression(inner);
        e = e.substr(0, op) + to_string(inner_val) + e.substr(cp + 1);
    }

    // 从右到左找 + -
    int op_pos = -1;
    char op_ch = 0;
    for (int i = (int)e.size() - 1; i >= 0; i--) {
        if (e[i] == '+' || e[i] == '-') {
            if (e[i] == '-') {
                if (i == 0) continue;
                char p = e[i-1];
                if (p == '+' || p == '-' || p == '*' || p == '/' || p == '^') continue;
            }
            op_pos = i; op_ch = e[i]; break;
        }
    }

    if (op_pos == -1) {
        for (int i = (int)e.size() - 1; i >= 0; i--) {
            if (e[i] == '*' || e[i] == '/') {
                op_pos = i; op_ch = e[i]; break;
            }
        }
    }

    if (op_pos == -1) {
        for (int i = (int)e.size() - 1; i >= 0; i--) {
            if (e[i] == '^') {
                op_pos = i; op_ch = e[i]; break;
            }
        }
    }

    if (op_pos == -1) {
        // 数组访问: varname[expr]
        size_t obrk = e.find('[');
        size_t cbrk = e.find(']');
        if (obrk != string::npos && cbrk > obrk) {
            string arr_name = e.substr(0, obrk);
            string idx_str  = e.substr(obrk + 1, cbrk - obrk - 1);
            if (variables.find(arr_name) != variables.end() &&
                variables[arr_name].type == ValType::Array) {
                int64_t idx = eval_expression(idx_str);
                if (idx < 0 || (size_t)idx >= variables[arr_name].arr_val.size())
                    python_error("array index " + to_string(idx) + " out of bounds for '" +
                                 arr_name + "' (size " + to_string(variables[arr_name].arr_val.size()) + ")");
                return variables[arr_name].arr_val[(size_t)idx].toInt64();
            }
        }

        // 变量引用（裸名称）
        if (variables.find(e) != variables.end())
            return variables[e].toInt64();

        if (e == "TIME")
            return (int64_t)time(nullptr);

        // 可能是字面量
        size_t p = 0;
        Value v = parse_literal(e, p);
        if (p == e.size()) return v.toInt64();

        python_error("unable to parse expression: " + expr);
        return 0;
    }

    string left_s  = e.substr(0, op_pos);
    string right_s = e.substr(op_pos + 1);
    if (left_s.empty())  left_s  = "0";

    int64_t left  = eval_expression(left_s);
    int64_t right = eval_expression(right_s);

    switch (op_ch) {
        case '+': return left + right;
        case '-': return left - right;
        case '*': return left * right;
        case '/': if (right == 0) python_error("division by zero"); return left / right;
        case '^': return (int64_t)pow((double)left, (double)right);
        default:  python_error("unknown operator '" + string(1, op_ch) + "' in expression"); return 0;
    }
}

// ============================================================
//  处理 format 字符串（替换 {var}，处理转义）
// ============================================================
string process_format_string(const string& fmt) {
    string result;
    size_t pos = 0;
    while (pos < fmt.size()) {
        if (fmt[pos] == '{') {
            size_t end = fmt.find('}', pos + 1);
            if (end != string::npos) {
                string content = trim(fmt.substr(pos + 1, end - pos - 1));

                // 数组访问: arr[idx]
                size_t ob = content.find('[');
                size_t cb = content.find(']');
                if (ob != string::npos && cb > ob) {
                    string arr_name = trim(content.substr(0, ob));
                    string idx_str  = trim(content.substr(ob + 1, cb - ob - 1));
                    if (variables.find(arr_name) != variables.end() &&
                        variables[arr_name].type == ValType::Array) {
                        int64_t idx = eval_expression(idx_str);
                        if (idx < 0 || (size_t)idx >= variables[arr_name].arr_val.size())
                            python_error("array index " + to_string(idx) + " out of bounds for '" +
                                         arr_name + "' in format string");
                        result += variables[arr_name].arr_val[(size_t)idx].toString();
                        pos = end + 1;
                        continue;
                    }
                }

                // 普通变量 / TIME
                if (content == "TIME") {
                    result += get_current_date_ymd();
                } else if (variables.find(content) != variables.end()) {
                    result += variables[content].toString();
                } else {
                    // 尝试作为内联表达式求值
                    try {
                        int64_t ival = eval_expression(content);
                        result += to_string(ival);
                    } catch (...) {
                        python_error("undefined variable or invalid expression '" + content + "' in format string");
                    }
                }
                pos = end + 1;
                continue;
            }
        }
        result += fmt[pos];
        pos++;
    }
    return result;
}

// ============================================================
//  判断条件
// ============================================================
bool evaluate_condition(const string& cond) {
    // 支持 == != < > <= >=
    string c = cond;
    c.erase(remove_if(c.begin(), c.end(), ::isspace), c.end());
    if (c.empty()) return false;

    // 查找比较运算符
    size_t pos = string::npos;
    string op;
    if ((pos = c.find("==")) != string::npos) op = "==";
    else if ((pos = c.find("!=")) != string::npos) op = "!=";
    else if ((pos = c.find("<=")) != string::npos) op = "<=";
    else if ((pos = c.find(">=")) != string::npos) op = ">=";
    else if ((pos = c.find('<'))  != string::npos) op = "<";
    else if ((pos = c.find('>'))  != string::npos) op = ">";

    if (op.empty()) {
        // 没有比较运算符 → 表达式求值，非零为真
        return eval_expression(cond) != 0;
    }

    string left_s  = c.substr(0, pos);
    string right_s = c.substr(pos + op.size());

    int64_t left  = eval_expression(left_s);
    int64_t right = eval_expression(right_s);

    if (op == "==") return left == right;
    if (op == "!=") return left != right;
    if (op == "<")  return left <  right;
    if (op == ">")  return left >  right;
    if (op == "<=") return left <= right;
    if (op == ">=") return left >= right;
    return false;
}

// ============================================================
//  注释剥离 & 括号参数提取
// ============================================================
// 去除 // 和 /* */ 注释
string strip_comments(const string& s) {
    string r;
    size_t i = 0;
    while (i < s.size()) {
        if (i + 1 < s.size() && s[i] == '/' && s[i+1] == '/') break; // 行注释到结尾
        if (i + 1 < s.size() && s[i] == '/' && s[i+1] == '*') {
            i += 2;
            while (i + 1 < s.size() && !(s[i] == '*' && s[i+1] == '/')) i++;
            if (i + 1 < s.size()) i += 2; // skip */
            continue;
        }
        r += s[i];
        i++;
    }
    return r;
}

// 提取括号参数: "a, b, c)" → {"a", "b", "c"}
vector<string> extract_paren_args(const string& s) {
    vector<string> result;
    string cur;
    int depth = 0;
    bool in_str = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '"') { in_str = !in_str; cur += c; continue; }
        if (in_str) { cur += c; continue; }
        if (c == '(') { depth++; if (depth == 1) continue; }
        if (c == ')') {
            if (depth == 1) { result.push_back(trim(cur)); return result; }
            depth--;
        }
        if (c == ',' && depth == 1) { result.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    return result;
}

// 从语句 s 中提取 keyword(args) 的 args
string extract_paren_content(const string& s, size_t after_kw) {
    // after_kw 指向 keyword 末尾下一个字符（即 '(' 的位置）
    if (after_kw >= s.size() || s[after_kw] != '(') return "";
    size_t depth = 1;
    size_t i = after_kw + 1;
    bool in_str = false;
    while (i < s.size() && depth > 0) {
        if (s[i] == '"') in_str = !in_str;
        else if (!in_str) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') {
                depth--;
                if (depth == 0) { return s.substr(after_kw + 1, i - after_kw - 1); }
            }
        }
        i++;
    }
    return "";
}

// ============================================================
//  值感知表达式求值（返回 Value，支持字符串 + .sub() + 函数调用）
// ============================================================
Value evaluate_value(const string& expr) {
    string e = trim(expr);
    if (e.empty()) return Value::make_int32(0);

    // 方法调用: varname.method(args) 或 "string".method(args)
    {
        size_t dot = e.find('.');
        if (dot != string::npos && dot > 0) {
            // 跳过字符串字面量内部的点号: "text.text" 不是方法调用
            int qc = 0;
            for (size_t i = 0; i < dot; i++) if (e[i] == '"') qc++;
            if (qc % 2 == 1) goto skip_method_call;
            string left = trim(e.substr(0, dot));
            string rest = e.substr(dot + 1);
            size_t mparen = rest.find('(');
            if (mparen != string::npos) {
                string method = rest.substr(0, mparen);
                string m_args = extract_paren_content(e, (int)(dot + 1 + mparen));
                vector<string> ma = extract_paren_args("(" + m_args + ")");

                // 获取源字符串（变量或字面量）
                string src;
                if (left[0] == '"') {
                    size_t lp = 0;
                    src = parse_string_content(left, lp);
                } else if (variables.find(left) != variables.end()) {
                    src = variables[left].toString();
                } else {
                    goto not_method_call;
                }

            // ---- 字符串方法 ----
            // .sub(a, b) — 已定义：下标 a 到 b
            if (method == "sub") {
                if (ma.size() < 2) python_error(".sub(a,b) needs 2 args");
                int64_t a = eval_expression(ma[0]);
                int64_t b = eval_expression(ma[1]);
                if (a < 0 || b > (int64_t)src.size() || a > b)
                    python_error(".sub() index out of bounds");
                return Value::make_string(src.substr((size_t)a, (size_t)(b - a)));
            }
            // .size() / .length()
            if (method == "size" || method == "length")
                return Value::make_int32((int32_t)src.size());
            // .empty()
            if (method == "empty")
                return Value::make_int32(src.empty() ? 1 : 0);
            // .find(sub[, pos])
            if (method == "find") {
                string sub = evaluate_value(ma[0]).toString();
                size_t pos = (ma.size() > 1) ? (size_t)eval_expression(ma[1]) : 0;
                size_t r = src.find(sub, pos);
                return Value::make_int32((r == string::npos) ? -1 : (int32_t)r);
            }
            // .rfind(sub[, pos])
            if (method == "rfind") {
                string sub = evaluate_value(ma[0]).toString();
                size_t pos = (ma.size() > 1) ? (size_t)eval_expression(ma[1]) : string::npos;
                size_t r = src.rfind(sub, pos);
                return Value::make_int32((r == string::npos) ? -1 : (int32_t)r);
            }
            // .substr(pos, len)
            if (method == "substr") {
                size_t p = (size_t)eval_expression(ma[0]);
                size_t l = (ma.size() > 1) ? (size_t)eval_expression(ma[1]) : string::npos;
                return Value::make_string(src.substr(p, l));
            }
            // .replace(pos, len, str)
            if (method == "replace") {
                size_t p = (size_t)eval_expression(ma[0]);
                size_t l = (size_t)eval_expression(ma[1]);
                string s = evaluate_value(ma[2]).toString();
                string r = src; r.replace(p, l, s);
                return Value::make_string(r);
            }
            // .append(str)
            if (method == "append") {
                string s = evaluate_value(ma[0]).toString();
                return Value::make_string(src + s);
            }
            // .insert(pos, str)
            if (method == "insert") {
                size_t p = (size_t)eval_expression(ma[0]);
                string s = evaluate_value(ma[1]).toString();
                string r = src; r.insert(p, s);
                return Value::make_string(r);
            }
            // .erase(pos, len)
            if (method == "erase") {
                size_t p = (size_t)eval_expression(ma[0]);
                size_t l = (ma.size() > 1) ? (size_t)eval_expression(ma[1]) : string::npos;
                string r = src; r.erase(p, l);
                return Value::make_string(r);
            }
            // .compare(str)
            if (method == "compare") {
                string s = evaluate_value(ma[0]).toString();
                int c = src.compare(s);
                return Value::make_int32((c < 0) ? -1 : (c > 0) ? 1 : 0);
            }
            // .starts_with(str) / .ends_with(str)
            if (method == "starts_with") {
                string s = evaluate_value(ma[0]).toString();
                return Value::make_int32(src.size() >= s.size() && src.substr(0, s.size()) == s ? 1 : 0);
            }
            if (method == "ends_with") {
                string s = evaluate_value(ma[0]).toString();
                return Value::make_int32(src.size() >= s.size() && src.substr(src.size()-s.size()) == s ? 1 : 0);
            }
        }
        } // close if(dot)
    } // close bare block

skip_method_call:
not_method_call:
    // 字符串字面量
    if (e[0] == '"') {
        size_t p = 0;
        return Value::make_string(parse_string_content(e, p));
    }

    // 函数调用: name(args)
    size_t paren = e.find('(');
    if (paren != string::npos) {
        string fname = trim(e.substr(0, paren));
        if (user_functions.find(fname) != user_functions.end()) {
            string call_args = extract_paren_content(e, (int)paren);
            return call_user_function(fname, call_args);
        }
    }

    // 带 + 的字符串拼接: "a" + "b" 或 var + "suffix"
    // 检查是否有 + 号且存在字符串上下文
    bool has_string = false;
    for (size_t i = 0; i < e.size(); i++) {
        if (e[i] == '"') { has_string = true; break; }
    }
    if (has_string) {
        // 按顶层 + 分割（跳出字符串和括号）
        vector<string> parts;
        string cur;
        int depth = 0;
        bool in_str = false;
        for (size_t i = 0; i < e.size(); i++) {
            char c = e[i];
            if (c == '"') { in_str = !in_str; cur += c; continue; }
            if (in_str) { cur += c; continue; }
            if (c == '(') { depth++; cur += c; continue; }
            if (c == ')') { depth--; cur += c; continue; }
            if (c == '+' && depth == 0) {
                parts.push_back(trim(cur));
                cur.clear();
                continue;
            }
            cur += c;
        }
        if (!cur.empty()) parts.push_back(trim(cur));
        if (parts.size() > 1) {
            string concat;
            for (const string& part : parts) {
                Value pv = evaluate_value(part);
                concat += pv.toString();
            }
            return Value::make_string(concat);
        }
    }

    // 变量引用
    if (variables.find(e) != variables.end())
        return variables[e];

    // 普通数值表达式
    int64_t ival = eval_expression(e);
    return Value::make_int32((int32_t)ival);
}

// ============================================================
//  调用用户函数
// ============================================================
Value call_user_function(const string& fname, const string& args_str) {
    FuncDef& fd = user_functions[fname];
    vector<string> arg_vals = extract_paren_args("(" + args_str + ")");
    if (arg_vals.size() != fd.param_names.size())
        python_error("function '" + fname + "' expects " + to_string(fd.param_names.size()) +
                     " arguments but got " + to_string(arg_vals.size()));

    // 保存当前变量（仅保存被参数覆盖的变量）
    map<string, Value> saved;
    for (const string& pn : fd.param_names) {
        if (variables.find(pn) != variables.end()) saved[pn] = variables[pn];
    }
    // 保存 return 状态
    bool old_returned = function_returned;
    Value old_retval = function_return_value;
    function_returned = false;

    // 设置参数
    for (size_t i = 0; i < fd.param_names.size(); i++) {
        variables[fd.param_names[i]] = evaluate_value(arg_vals[i]);
    }

    // 执行函数体（使用 execute_program_lines 支持多行 if 块等）
    function_returned = false;
    execute_program_lines(fd.body_lines, 0);

    Value result = function_returned ? function_return_value : Value::make_int32(0);

    // 恢复变量
    for (const string& pn : fd.param_names) {
        if (saved.find(pn) != saved.end()) variables[pn] = saved[pn];
        else variables.erase(pn);
    }
    function_returned = old_returned;
    function_return_value = old_retval;

    return result;
}

// ============================================================
//  执行一条语句（已拼接好的完整单行）
// ============================================================
// 返回 false 表示退出
bool execute_statement(const string& stmt) {
    // 1) 剥离注释
    string clean = strip_comments(stmt);
    string s = trim(clean);
    if (s.empty()) return true;
    if (s.back() == ';') s.pop_back();
    s = trim(s);
    if (s.empty()) return true;

    // ---- global directives (keep $$ syntax) ----
    if (s.size() >= 2 && s.front() == '$' && s.back() == '$') {
        string content = trim(s.substr(1, s.size() - 2));
        if (content.find("fullScreen") == 0) {
            string arg = trim(content.substr(10));
            if (arg == "on") {
                if (!fullscreen_enabled) {
                    fullscreen_enabled = true;
                    if (traceLogEnabled) log_event("fullscreen enabled");
                }
            } else if (arg == "off") {
                if (fullscreen_enabled) {
                    fullscreen_enabled = false;
                    if (traceLogEnabled) log_event("fullscreen disabled");
                }
            }
            return true;
        } else if (content.find("traceLog") == 0) {
            string arg = trim(content.substr(8));
            if (arg == "on")  traceLogEnabled = true;
            else if (arg == "off") traceLogEnabled = false;
            return true;
        } else if (content.find("debugMessage") == 0) {
            string arg = trim(content.substr(12));
            if (arg == "on")  debugMessageEnabled = true;
            else if (arg == "off") debugMessageEnabled = false;
            return true;
        }
        system(content.c_str());
        return true;
    }

    // ---- using "path" / using <header> — 引入头文件 ---- //
    if (s.find("using ") == 0 || s.find("using\t") == 0) {
        string rest = trim(s.substr(5));
        string filepath;
        bool found = false;

        if (rest.size() >= 2 && rest[0] == '"' && rest.back() == '"') {
            // using "..." — 绝对/相对路径
            size_t pp = 0;
            filepath = parse_string_content(rest, pp);
            if (filepath.empty()) python_error("using requires a valid filename");
            // 相对路径：拼上当前脚本目录
            if (filepath.find(":\\") == string::npos && filepath[0] != '\\') {
                if (filepath.size() >= 2 && filepath[0] == '.' && (filepath[1] == '/' || filepath[1] == '\\'))
                    filepath = currentScriptDir + filepath.substr(2);
                else
                    filepath = currentScriptDir + filepath;
            }
            // 尝试文件本身，再尝试加 .lth
            if (filesystem::exists(filepath)) found = true;
            else {
                string alt = filepath + ".lth";
                if (filesystem::exists(alt)) { filepath = alt; found = true; }
            }
        } else if (rest.size() >= 2 && rest[0] == '<' && rest.back() == '>') {
            // using <header> — 从 headers/ 目录查找
            string name = rest.substr(1, rest.size() - 2);
            filepath = get_exe_dir() + "headers\\" + name;
            if (filesystem::exists(filepath)) found = true;
            else {
                string alt = filepath + ".lth";
                if (filesystem::exists(alt)) { filepath = alt; found = true; }
            }
        } else {
            python_error("invalid using syntax, use using \"path\" or using <header>");
        }

        if (!found) python_error("header file not found: " + filepath);

        ifstream hf(filepath);
        if (!hf.is_open()) python_error("cannot open header file: " + filepath);

        string old_dir = currentScriptDir;
        currentScriptDir = filepath.substr(0, filepath.find_last_of("/\\") + 1);

        vector<string> hlines;
        string hl;
        while (getline(hf, hl)) hlines.push_back(hl);
        hf.close();

        // 处理开头全局指令
        size_t hidx = 0;
        for (; hidx < hlines.size(); ++hidx) {
            string t = trim(hlines[hidx]);
            if (t == "$fullScreen on$" || t == "$fullScreen off$" ||
                t == "$traceLog on$"   || t == "$debugMessage on$") continue;
            break;
        }
        execute_program_lines(hlines, hidx);
        currentScriptDir = old_dir;

        if (traceLogEnabled) log_event("using header: " + filepath);
        return true;
    }

    // ---- define statement (variables, arrays, filestreams, functions) ----
    if (s.find("define") == 0 && s.size() > 6 &&
        (s[6] == ' ' || s[6] == '\t')) {
        string after = trim(s.substr(6));

        // --- 函数定义: funcname(params) {: 或 funcname(params):{ --- //
        {
            size_t paren = after.find('(');
            if (paren != string::npos) {
                string fname = trim(after.substr(0, paren));
                if (is_valid_varname(fname) && paren > 0 && after[0] != '[') {
                    size_t close_p = after.find(')', paren);
                    if (close_p == string::npos)
                        python_error("function definition missing ')'");
                    string params_str = after.substr(paren + 1, close_p - paren - 1);
                    vector<string> params = extract_paren_args("(" + params_str + ")");
                    // 查找 {:  或 :{  或 {
                    string rest = trim(after.substr(close_p + 1));
                    size_t brace = rest.find('{');
                    if (brace == string::npos)
                        python_error("function definition requires { body }");
                    string body_content = rest.substr(brace + 1);
                    // 找到匹配的 }
                    size_t close_brace = body_content.rfind('}');
                    if (close_brace == string::npos)
                        python_error("function definition missing }");
                    body_content = body_content.substr(0, close_brace);
                    // 分割成行
                    vector<string> body_lines;
                    istringstream bs(body_content);
                    string bl;
                    while (getline(bs, bl)) {
                        string tb = trim(bl);
                        if (!tb.empty()) body_lines.push_back(tb);
                    }
                    FuncDef fd;
                    fd.param_names = params;
                    fd.body_lines = body_lines;
                    user_functions[fname] = fd;
                    if (traceLogEnabled)
                        log_event("define function " + fname + "(" + to_string(params.size()) + " params)");
                    return true;
                }
            }
        }

        // 检查数组语法: name[size]
        size_t bracket = after.find('[');
        if (bracket != string::npos) {
            string name = trim(after.substr(0, bracket));
            size_t cb = after.find(']', bracket);
            if (cb == string::npos)
                python_error("'define' array missing closing ']'");
            string size_str = trim(after.substr(bracket + 1, cb - bracket - 1));
            if (!is_valid_varname(name))
                python_error("invalid array variable name '" + name + "'");
            int64_t arr_size = eval_expression(size_str);
            if (arr_size <= 1)
                python_error("array size must be > 1, got " + to_string(arr_size));
            size_t eq = after.find('=', cb);
            if (eq != string::npos) {
                string init_str = trim(after.substr(eq + 1));
                vector<Value> arr = parse_array_literal(init_str, (size_t)arr_size);
                variables[name] = Value::make_array(arr);
                if (traceLogEnabled)
                    log_event("define array " + name + "[" + to_string(arr_size) + "] initialized");
            } else {
                variables[name] = Value::make_array(
                    vector<Value>((size_t)arr_size, Value::make_int32(0)));
                if (traceLogEnabled)
                    log_event("define array " + name + "[" + to_string(arr_size) + "] = all 0");
            }
            return true;
        }

        // 简单变量: 检查可选的 = initializer
        size_t eq = after.find('=');
        if (eq != string::npos) {
            string varname = trim(after.substr(0, eq));
            string val_str  = trim(after.substr(eq + 1));
            if (!is_valid_varname(varname))
                python_error("invalid variable name '" + varname + "'");
            variables[varname] = evaluate_value(val_str);
            if (traceLogEnabled)
                log_event("define variable " + varname + " = " + variables[varname].toString());
            return true;
        }

        // 普通 define 或 define filestream
        string varname = after;
        if (varname.empty()) {
            python_error("'define' requires a variable name");
        } else if (after.find("filestream ") == 0 || after.find("filestream\t") == 0) {
            string fsname = trim(after.substr(10));
            if (!is_valid_varname(fsname))
                python_error("invalid filestream name '" + fsname + "'");
            variables[fsname] = Value::make_filestream();
            fileStreamMap[fsname] = nullptr;
            if (traceLogEnabled)
                log_event("define filestream " + fsname);
        } else if (is_valid_varname(varname)) {
            variables[varname] = Value::make_int32(0);
            if (traceLogEnabled)
                log_event("define variable " + varname + " = 0");
        } else {
            python_error("invalid variable name '" + varname + "'");
        }
        return true;
    }

    // ---- output.file("path") — 重定向输出到文件 ---- //
    if (s.find("output.file(") == 0) {
        size_t p = 12; // skip "output.file("
        string path = parse_string_content(s, p);
        if (outputRedirectEnabled) outputRedirectFile.close();
        outputRedirectFile.open(path);
        if (!outputRedirectFile.is_open())
            python_error("cannot open output file '" + path + "'");
        outputRedirectEnabled = true;
        if (traceLogEnabled) log_event("output redirected to file: " + path);
        return true;
    }
    // ---- output.fileClose() — 关闭输出重定向 ---- //
    if (s.find("output.fileClose") == 0) {
        if (outputRedirectEnabled) {
            outputRedirectFile.close();
            outputRedirectEnabled = false;
        }
        if (traceLogEnabled) log_event("output file redirection closed");
        return true;
    }
    // ---- output.fileStream(stA, value) — 向文件流输出 ---- //
    if (s.find("output.fileStream(") == 0) {
        string args = s.substr(18); // content after "output.fileStream("
        if (args.empty() || args.back() != ')') python_error("output.fileStream() missing ')'");
        args.pop_back(); // remove ')'
        size_t comma = args.find(',');
        if (comma == string::npos) python_error("output.fileStream() requires 2 arguments");
        string fsname = trim(args.substr(0, comma));
        string out_val = trim(args.substr(comma + 1));
        if (variables.find(fsname) == variables.end() || variables[fsname].type != ValType::FileStream)
            python_error("'" + fsname + "' is not a filestream");
        if (fileStreamMap[fsname] == nullptr) {
            fileStreamMap[fsname] = new fstream(fsname, ios::out | ios::app);
            if (!fileStreamMap[fsname]->is_open())
                python_error("cannot open filestream file '" + fsname + "' for writing");
        }
        // 解析要写入的值
        string write_str;
        size_t vp = 0;
        try {
            Value v = parse_literal(out_val, vp);
            if (vp == out_val.size()) { write_str = v.toString(); }
        } catch (...) {}
        if (write_str.empty()) {
            if (variables.find(out_val) != variables.end())
                write_str = variables[out_val].toString();
            else {
                int64_t ival = eval_expression(out_val);
                write_str = to_string(ival);
            }
        }
        (*fileStreamMap[fsname]) << write_str << flush;
        if (traceLogEnabled) log_event("output.fileStream " + fsname + " << " + write_str);
        return true;
    }

    // ---- output(args...) — 多参数拼接输出 ---- //
    if (s.find("output(") == 0) {
        string args_content = extract_paren_content(s, 6);
        if (args_content.empty()) python_error("output() missing arguments");
        vector<string> args = extract_paren_args("(" + args_content + ")");

        string out_str;
        for (const string& arg : args) {
            // output(format("..."))
            if (arg.find("format(") == 0) {
                string fmt_content = extract_paren_content(arg, 6);
                out_str += process_format_string(fmt_content);
            } else {
                // output("string"), output(var), output(expr), output(var.sub(a,b))
                out_str += evaluate_value(arg).toString();
            }
        }
        if (outputRedirectEnabled)
            outputRedirectFile << out_str << flush;
        else
            print_with_color_codes(out_str);
        if (traceLogEnabled) log_event("output: " + out_str);
        return true;
    }

    // ---- input.fileStream(stA, value) — 从文件流读取标记 ---- //
    if (s.find("input.fileStream(") == 0) {
        string args_content = extract_paren_content(s, 17);
        vector<string> args = extract_paren_args("(" + args_content + ")");
        if (args.size() < 2) python_error("input.fileStream(stA, var) requires 2 arguments");
        string fsname = args[0];
        string vname  = args[1];
        if (variables.find(fsname) == variables.end() || variables[fsname].type != ValType::FileStream)
            python_error("'" + fsname + "' is not a filestream");
        if (variables.find(vname) == variables.end())
            python_error("undefined variable '" + vname + "' in input.fileStream()");
        if (fileStreamMap[fsname] == nullptr) {
            fileStreamMap[fsname] = new fstream(fsname, ios::in);
            if (!fileStreamMap[fsname]->is_open())
                python_error("cannot open filestream file '" + fsname + "' for reading");
        }
        string token;
        (*fileStreamMap[fsname]) >> token;
        size_t tp = 0;
        try {
            Value tv = parse_literal(token, tp);
            if (tp == token.size()) { variables[vname] = tv; }
            else { variables[vname] = Value::make_string(token); }
        } catch (...) {
            int64_t ival = eval_expression(token);
            variables[vname] = Value::make_int32((int32_t)ival);
        }
        if (traceLogEnabled) log_event("input.fileStream " + fsname + " >> " + vname + " = " + variables[vname].toString());
        return true;
    }

    // ---- inputLine(varname, "prompt") — 从标准输入读取一行 ---- //
    if (s.find("inputLine(") == 0 && s.find("inputLine.fileStream(") != 0) {
        string args_content = extract_paren_content(s, 9);
        vector<string> args = extract_paren_args("(" + args_content + ")");
        if (args.empty()) python_error("inputLine() requires a variable name");
        string vname = args[0];
        string prompt_text = (args.size() > 1) ? args[1] : ""; // TODO: parse_string_content

        if (variables.find(vname) == variables.end())
            python_error("undefined variable '" + vname + "' in inputLine()");
        if (prompt_text.empty()) prompt_text = "> ";
        if (debugMessageEnabled) cout << prompt_text << flush;
        string line;
        getline(cin, line);
        line = trim(line);
        if (line.empty()) { variables[vname] = Value::make_int32(0); }
        else {
            size_t lp = 0;
            try {
                Value lv = parse_literal(line, lp);
                if (lp == line.size()) { variables[vname] = lv; }
                else { variables[vname] = Value::make_string(line); }
            } catch (...) {
                int64_t ival = eval_expression(line);
                variables[vname] = Value::make_int32((int32_t)ival);
            }
        }
        if (traceLogEnabled) log_event("inputLine " + vname + " = " + variables[vname].toString());
        return true;
    }

    // ---- inputLine.fileStream(stA, varname) — 从文件流读取一行 ---- //
    if (s.find("inputLine.fileStream(") == 0) {
        string args_content = extract_paren_content(s, 21);
        vector<string> args = extract_paren_args("(" + args_content + ")");
        if (args.size() < 2) python_error("inputLine.fileStream(stA, var) requires 2 arguments");
        string fsname = args[0];
        string vname  = args[1];
        if (variables.find(fsname) == variables.end() || variables[fsname].type != ValType::FileStream)
            python_error("'" + fsname + "' is not a filestream");
        if (variables.find(vname) == variables.end())
            python_error("undefined variable '" + vname + "' in inputLine.fileStream()");
        if (fileStreamMap[fsname] == nullptr) {
            fileStreamMap[fsname] = new fstream(fsname, ios::in);
            if (!fileStreamMap[fsname]->is_open())
                python_error("cannot open filestream file '" + fsname + "' for reading");
        }
        string line;
        getline(*fileStreamMap[fsname], line);
        line = trim(line);
        if (line.empty()) { variables[vname] = Value::make_int32(0); }
        else {
            size_t lp = 0;
            try {
                Value lv = parse_literal(line, lp);
                if (lp == line.size()) { variables[vname] = lv; }
                else { variables[vname] = Value::make_string(line); }
            } catch (...) {
                int64_t ival = eval_expression(line);
                variables[vname] = Value::make_int32((int32_t)ival);
            }
        }
        if (traceLogEnabled) log_event("inputLine.fileStream " + fsname + " >> " + vname + " = " + variables[vname].toString());
        return true;
    }

    // ---- input(varname, "prompt") — 从标准输入读取标记 ---- //
    if (s.find("input(") == 0) {
        string args_content = extract_paren_content(s, 5);
        vector<string> args = extract_paren_args("(" + args_content + ")");
        if (args.empty()) python_error("input(var) requires a variable name");
        string varname = args[0];
        if (variables.find(varname) == variables.end())
            python_error("undefined variable '" + varname + "' in input()");
        string prompt_text = (args.size() > 1 && !args[1].empty()) ? args[1] : ("input " + varname + ": ");
        if (debugMessageEnabled) cout << prompt_text << flush;
        string input_line;
        getline(cin, input_line);
        input_line = trim(input_line);
        if (input_line.empty()) { variables[varname] = Value::make_int32(0); }
        else {
            size_t ip = 0;
            try {
                Value inv = parse_literal(input_line, ip);
                if (ip == input_line.size()) { variables[varname] = inv; }
                else {
                    int64_t ival = eval_expression(input_line);
                    variables[varname] = Value::make_int32((int32_t)ival);
                }
            } catch (...) {
                int64_t ival = eval_expression(input_line);
                variables[varname] = Value::make_int32((int32_t)ival);
            }
        }
        if (traceLogEnabled) log_event("input " + varname + " = " + variables[varname].toString());
        return true;
    }

    // ---- sleep(ms) ---- //
    if (s.find("sleep(") == 0) {
        string args_content = extract_paren_content(s, 5);
        int64_t ms = eval_expression(args_content);
        if (traceLogEnabled) log_event("sleep " + to_string(ms) + " ms");
        Sleep((DWORD)ms);
        return true;
    }

    // ---- quit() / quit(ms) ---- //
    if (s.find("quit(") == 0) {
        string args_content = extract_paren_content(s, 4);
        if (args_content.empty()) {
            if (traceLogEnabled) log_event("quit immediately");
            return false;
        }
        int64_t ms = eval_expression(args_content);
        if (traceLogEnabled) log_event("quit after " + to_string(ms) + " ms");
        Sleep((DWORD)ms);
        return false;
    }

    // ---- system("command") — 执行系统命令 ---- //
    if (s.find("system(") == 0) {
        string args_content = extract_paren_content(s, 6);
        if (args_content.empty()) python_error("system() requires a command string");
        if (traceLogEnabled) log_event("system: " + args_content);
        system(args_content.c_str());
        return true;
    }

    // ---- name.open("filename") / name.close() — 文件流打开/关闭 ---- //
    for (auto& kv : fileStreamMap) {
        const string& fsname = kv.first;
        string open_pat  = fsname + ".open(";
        string close_pat = fsname + ".close";
        if (s.find(open_pat) == 0) {
            string args_content = extract_paren_content(s, (int)fsname.size() + 5);
            if (args_content.empty()) python_error(fsname + ".open() requires a filename");
            fstream*& fs = fileStreamMap[fsname];
            if (fs) { fs->close(); delete fs; }
            fs = new fstream(args_content, ios::in | ios::out | ios::app);
            if (!fs->is_open())
                python_error("cannot open file '" + args_content + "' for " + fsname);
            if (traceLogEnabled) log_event(fsname + ".open(" + args_content + ")");
            return true;
        }
        if (s.find(close_pat) == 0) {
            fstream*& fs = fileStreamMap[fsname];
            if (fs) { fs->close(); delete fs; fs = nullptr; }
            if (traceLogEnabled) log_event(fsname + ".close()");
            return true;
        }
    }

    // ---- colorfulText(color, text) — 彩色文本输出（不换行） ---- //
    if (s.find("colorfulText(") == 0) {
        string args_content = extract_paren_content(s, 12);
        vector<string> args = extract_paren_args("(" + args_content + ")");
        if (args.size() < 2) python_error("colorfulText(color, text) requires 2 arguments");
        string color_str = trim(args[0]);
        // 去除引号（来自 "C" 字面量）
        if (color_str.size() >= 2 && color_str[0] == '"' && color_str.back() == '"') {
            size_t qp = 0;
            color_str = parse_string_content(color_str, qp);
        }
        string text_val  = evaluate_value(args[1]).toString();
        if (color_str.size() != 1) python_error("colorfulText: color must be 0~F, got '" + color_str + "'");
        char color_char = toupper((unsigned char)color_str[0]);
        bool valid = (color_char >= '0' && color_char <= '9') || (color_char >= 'A' && color_char <= 'F');
        if (!valid) python_error("colorfulText: color must be 0~F, got '" + string(1, color_char) + "'");
        // 使用颜色代码前缀输出
        string colored = string("%") + color_char + "{" + text_val + "}%";
        if (outputRedirectEnabled)
            outputRedirectFile << text_val << flush;
        else
            print_with_color_codes(colored);
        if (traceLogEnabled) log_event("colorfulText: " + text_val);
        return true;
    }

    // ---- array.push(value) / array.delete(idx) — 数组操作 ---- //
    for (auto& kv : variables) {
        if (kv.second.type != ValType::Array) continue;
        const string& arr_name = kv.first;
        string push_pat = arr_name + ".push(";
        string del_pat  = arr_name + ".delete(";
        // .push(val)
        if (s.find(push_pat) == 0) {
            string args_content = extract_paren_content(s, (int)arr_name.size() + 5);
            Value v = evaluate_value(args_content);
            variables[arr_name].arr_val.push_back(v);
            if (traceLogEnabled) log_event(arr_name + ".push(" + v.toString() + ")");
            return true;
        }
        // .delete(idx)
        if (s.find(del_pat) == 0) {
            string args_content = extract_paren_content(s, (int)arr_name.size() + 7);
            int64_t idx = eval_expression(args_content);
            if (idx < 0 || (size_t)idx >= variables[arr_name].arr_val.size())
                python_error(arr_name + ".delete(" + to_string(idx) + ") index out of bounds");
            variables[arr_name].arr_val.erase(variables[arr_name].arr_val.begin() + (size_t)idx);
            if (traceLogEnabled) log_event(arr_name + ".delete(" + to_string(idx) + ")");
            return true;
        }
    }

    // ---- 赋值语句 varname = expr 或 varname[expr] = expr ----
    size_t eq_pos = s.find('=');
    if (eq_pos != string::npos) {
        string left_s = trim(s.substr(0, eq_pos));
        string right_s = trim(s.substr(eq_pos + 1));
        if (!right_s.empty() && right_s.back() == ';') right_s.pop_back();
        right_s = trim(right_s);

        // --- 数组元素赋值: varname[expr] = value ---
        size_t lb = left_s.find('[');
        size_t rb = left_s.find(']');
        if (lb != string::npos && rb != string::npos && rb > lb) {
            string arr_name = trim(left_s.substr(0, lb));
            string idx_str  = trim(left_s.substr(lb + 1, rb - lb - 1));
            if (!is_valid_varname(arr_name))
                python_error("invalid array name '" + arr_name + "'");
            if (variables.find(arr_name) == variables.end())
                python_error("undefined variable '" + arr_name + "'");
            if (variables[arr_name].type != ValType::Array)
                python_error("'" + arr_name + "' is not an array");
            int64_t idx = eval_expression(idx_str);
            if (idx < 0 || (size_t)idx >= variables[arr_name].arr_val.size())
                python_error("array index " + to_string(idx) + " out of bounds for '" + arr_name +
                             "' (size " + to_string(variables[arr_name].arr_val.size()) + ")");
            variables[arr_name].arr_val[(size_t)idx] = evaluate_value(right_s);
            if (traceLogEnabled)
                log_event("assign " + arr_name + "[" + to_string(idx) + "] = " +
                          variables[arr_name].arr_val[(size_t)idx].toString());
            return true;
        }

        // --- 普通变量赋值 ---
        if (is_valid_varname(left_s)) {
            if (variables.find(left_s) == variables.end()) {
                python_error("undefined variable '" + left_s + "' on left side of assignment");
                return true;
            }
            variables[left_s] = evaluate_value(right_s);
            if (traceLogEnabled)
                log_event("assign " + left_s + " = " + variables[left_s].toString());
            return true;
        }
    }

    // ---- return expr — 函数返回值 ---- //
    if (s.find("return(") == 0 || s.find("return ") == 0 || s == "return") {
        string rest = s.substr(6);
        if (rest.empty() || rest.back() != ';') {
            // return; without paren
            function_return_value = Value::make_int32(0);
        } else {
            rest.pop_back();
            rest = trim(rest);
            if (!rest.empty() && rest[0] == '(' && rest.back() == ')')
                rest = rest.substr(1, rest.size() - 2);
            function_return_value = evaluate_value(trim(rest));
        }
        function_returned = true;
        return true;
    }

    // 未知语句
    python_error("unrecognized statement: '" + s + "'");
    return true;
}

// ============================================================
//  全屏控制（保留 v0.1 实现）
// ============================================================
void hide_taskbar() {
    HWND taskbar = FindWindow("Shell_TrayWnd", NULL);
    if (taskbar) ShowWindow(taskbar, SW_HIDE);
}

void show_taskbar() {
    HWND taskbar = FindWindow("Shell_TrayWnd", NULL);
    if (taskbar) ShowWindow(taskbar, SW_SHOW);
}

void fullscreen_console() {
    HWND console = GetConsoleWindow();
    if (!console) return;
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole == INVALID_HANDLE_VALUE) return;
    hide_taskbar();
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
        COORD maxSize = GetLargestConsoleWindowSize(hConsole);
        if (maxSize.X > 0) {
            COORD bufSize;
            bufSize.X = maxSize.X;
            bufSize.Y = max((SHORT)(csbi.dwCursorPosition.Y + 200), maxSize.Y);
            SetConsoleScreenBufferSize(hConsole, bufSize);
        }
    }
    COORD bufferSize = {0, 0};
    if (SetConsoleDisplayMode(hConsole, CONSOLE_FULLSCREEN_MODE, &bufferSize))
        return;
    LONG_PTR style = GetWindowLongPtr(console, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    SetWindowLongPtr(console, GWL_STYLE, style);
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    DEVMODE dm;
    dm.dmSize = sizeof(DEVMODE);
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
        screenW = dm.dmPelsWidth;
        screenH = dm.dmPelsHeight;
    }
    LONG_PTR exStyle = GetWindowLongPtr(console, GWL_EXSTYLE);
    exStyle &= ~WS_EX_WINDOWEDGE;
    SetWindowLongPtr(console, GWL_EXSTYLE, exStyle);
    SetWindowPos(console, HWND_TOP, 0, 0, screenW, screenH,
                 SWP_FRAMECHANGED | SWP_SHOWWINDOW | SWP_NOZORDER);
}

void restore_console_window() {
    HWND console = GetConsoleWindow();
    if (console) {
        LONG_PTR style = GetWindowLongPtr(console, GWL_STYLE);
        style |= WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU;
        SetWindowLongPtr(console, GWL_STYLE, style);
        LONG_PTR exStyle = GetWindowLongPtr(console, GWL_EXSTYLE);
        exStyle |= WS_EX_WINDOWEDGE;
        SetWindowLongPtr(console, GWL_EXSTYLE, exStyle);
        SetWindowPos(console, NULL, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOZORDER);
        ShowWindow(console, SW_RESTORE);
    }
    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hConsole != INVALID_HANDLE_VALUE) {
        SetConsoleDisplayMode(hConsole, CONSOLE_WINDOWED_MODE, NULL);
    }
    show_taskbar();
}

// ============================================================
//  If 块处理（多行）
// ============================================================
// 计算括号深度（跳过字符串字面量内的 {}）
static int brace_depth_skip_strings(const string& s) {
    int d = 0;
    bool in_str = false;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '"') { in_str = !in_str; continue; }
        if (in_str) continue;
        if (s[i] == '{') d++;
        if (s[i] == '}') d--;
    }
    return d;
}

// 从 line_idx 开始解析 if 块，返回处理后的下一行索引
size_t process_if_block(const vector<string>& lines, size_t line_idx, bool& continue_flag) {
    // ---- 读取 if 所在行及后续行，找到完整 if (cond) { ... } ---- //
    string full_text;
    size_t idx = line_idx;
    int depth = 0;
    bool found_open = false;
    // 先找到 ( 和 ) 确认条件是完整的
    // 再找到 { 然后跟踪到匹配的 }
    while (idx < lines.size()) {
        string l = lines[idx];
        full_text += l + "\n";
        int delta = brace_depth_skip_strings(l);
        depth += delta;
        if (delta > 0) found_open = true;
        if (found_open && depth == 0) {
            idx++;
            break;
        }
        idx++;
    }

    // 提取条件: if (condition)
    size_t cp = full_text.find('(');
    size_t cpe = full_text.find(')');
    if (cp == string::npos || cpe == string::npos)
        python_error("if 语法错误: 缺少括号");

    string cond_str = trim(full_text.substr(cp + 1, cpe - cp - 1));
    bool cond_result = evaluate_condition(cond_str);

    // 提取块内容: { ... }
    size_t ob = full_text.find('{', cpe);
    if (ob == string::npos) python_error("if 缺少 {");
    size_t cb = full_text.rfind('}');
    if (cb == string::npos) python_error("'if' block: expected '}' to close the block");
    string block_content = full_text.substr(ob + 1, cb - ob - 1);

    // 执行块内容
    if (cond_result) {
        istringstream stream(block_content);
        string bline;
        string stmt_accum;
        while (getline(stream, bline)) {
            string tb = trim(bline);
            if (tb.empty()) continue;
            stmt_accum += tb + " ";
            if (tb.back() == ';') {
                continue_flag = execute_statement(stmt_accum);
                stmt_accum.clear();
                if (!continue_flag) return idx;
            }
        }
        if (!stmt_accum.empty()) {
            continue_flag = execute_statement(stmt_accum);
        }
    }

    // ---- 检查 else ----
    size_t else_idx = idx;
    while (else_idx < lines.size()) {
        string tl = trim(lines[else_idx]);
        if (tl.empty()) { else_idx++; continue; }
        if (tl.find("else") == 0) {
            // 读 else 块
            string else_full;
            size_t ei = else_idx;
            depth = 0;
            bool found_else_brace = false;
            while (ei < lines.size()) {
                string el = lines[ei];
                else_full += el + "\n";
                int delta = brace_depth_skip_strings(el);
                depth += delta;
                if (delta > 0) found_else_brace = true;
                if (found_else_brace && depth == 0) {
                    ei++;
                    break;
                }
                ei++;
            }
            size_t e_ob = else_full.find('{');
            size_t e_cb = else_full.rfind('}');
            if (e_ob != string::npos && e_cb != string::npos) {
                string else_content = else_full.substr(e_ob + 1, e_cb - e_ob - 1);
                if (!cond_result) {
                    istringstream estream(else_content);
                    string eline;
                    string estmt;
                    while (getline(estream, eline)) {
                        string teb = trim(eline);
                        if (teb.empty()) continue;
                        estmt += teb + " ";
                        if (teb.back() == ';') {
                            continue_flag = execute_statement(estmt);
                            estmt.clear();
                            if (!continue_flag) return ei;
                        }
                    }
                    if (!estmt.empty()) {
                        continue_flag = execute_statement(estmt);
                    }
                }
            }
            return ei;
        }
        break;
    }

    return idx;
}

// ============================================================
//  执行已加载的脚本行（从 start_idx 开始）
// ============================================================
bool execute_program_lines(const vector<string>& lines, size_t start_idx) {
    bool continue_flag = true;
    size_t line_index = start_idx;
    while (line_index < lines.size() && continue_flag) {
        try {
            string raw_line = lines[line_index];
            string trimmed = trim(raw_line);
            if (trimmed.empty()) { line_index++; continue; }

            // 检测 if 块
            if (trimmed.find("if") == 0 &&
                trimmed.size() > 2 &&
                (trimmed[2] == ' ' || trimmed[2] == '(')) {
                line_index = process_if_block(lines, line_index, continue_flag);
                continue;
            }

            // 普通语句 / $...$ 指令：拼接字符直到 ; 或 $$
            string stmt_buffer;
            bool found_terminator = false;
            while (line_index < lines.size()) {
                string tl = trim(lines[line_index]);
                if (tl.empty()) { line_index++; continue; }

                stmt_buffer += tl + " ";

                if (tl.size() >= 2 && tl.front() == '$' && tl.back() == '$') {
                    found_terminator = true;
                    line_index++;
                    break;
                }
                if (!tl.empty() && tl.back() == ';') {
                    found_terminator = true;
                    line_index++;
                    break;
                }
                line_index++;
            }

            if (!found_terminator && !stmt_buffer.empty()) {
                python_error("missing semicolon or $...$ terminator in: " + stmt_buffer);
            } else if (found_terminator) {
                continue_flag = execute_statement(stmt_buffer);
            }
        } catch (const runtime_error& e) {
            // 跳过出错语句，继续执行后续代码
            cerr << "Skipping error: " << e.what() << endl;
            // 确保跳过当前语句的剩余行
            while (line_index < lines.size()) {
                string t = trim(lines[line_index]);
                if (!t.empty() && (t.back() == ';' || (t.size() >= 2 && t.front() == '$' && t.back() == '$'))) {
                    line_index++;
                    break;
                }
                line_index++;
            }
            continue_flag = true;
        }
    }
    return continue_flag;
}

// ============================================================
//  从 REPL 中运行一个 .lts 脚本文件
// ============================================================
void run_script_file(const string& path) {
    ifstream infile(path);
    if (!infile.is_open()) {
        python_error("cannot open file '" + path + "'");
        return;
    }

    vector<string> lines;
    string line;
    while (getline(infile, line)) {
        lines.push_back(line);
    }
    infile.close();

    // 处理开头的全局指令行（不重置已存在的标志，只处理 on 开关）
    size_t line_index = 0;
    for (; line_index < lines.size(); ++line_index) {
        string trimmed = trim(lines[line_index]);
        if (trimmed == "$fullScreen on$")   { fullscreen_enabled = true;  continue; }
        if (trimmed == "$fullScreen off$")  { fullscreen_enabled = false; continue; }
        if (trimmed == "$traceLog on$")     { traceLogEnabled = true;     continue; }
        if (trimmed == "$debugMessage on$") { debugMessageEnabled = true; continue; }
        break;
    }
    if (fullscreen_enabled) {
        fullscreen_console();
        hide_taskbar();
    }

    execute_program_lines(lines, line_index);

    if (fullscreen_enabled) {
        fullscreen_enabled = false;
        restore_console_window();
    }
}

// ============================================================
//  REPL 交互模式
// ============================================================
void repl_mode() {
    // 日志
    if (traceLogEnabled) {
        filesystem::create_directories("TraceLog");
        string filename = "TraceLog/" + get_current_timestamp() + "-TraceLog.txt";
        traceLogFile.open(filename);
        if (!traceLogFile.is_open()) {
            python_error("cannot create trace log file '" + filename + "': check directory permissions");
        } else {
            log_event("REPL session started");
        }
    }

    cout << "LetherScript v1.2 Interactive Mode (type 'exit' or 'quit' to leave)" << endl;
    cout << "Enter .lts filename to run a script, or type code directly." << endl;

    string input_buffer;
    while (true) {
        // 显示提示符
        if (input_buffer.empty())
            cout << "LetherScript>> " << flush;
        else
            cout << "           ... " << flush;

        string raw_line;
        if (!getline(cin, raw_line)) break; // EOF

        string trimmed = trim(raw_line);
        if (trimmed.empty()) continue;

        // 退出命令
        if (trimmed == "exit" || trimmed == "quit") {
            cout << "Leaving LetherScript interactive mode." << endl;
            break;
        }

        // 检查是否为 .lts 脚本文件路径
        if (trimmed.size() >= 4 && trimmed.substr(trimmed.size() - 4) == ".lts") {
            if (!input_buffer.empty()) {
                // 有缓存的代码，先执行
                input_buffer += ";"; // 补充终止符
                vector<string> fake_lines = { input_buffer };
                execute_program_lines(fake_lines, 0);
                input_buffer.clear();
            }
            run_script_file(trimmed);
            continue;
        }

        // 累加输入到缓冲区
        input_buffer += raw_line + "\n";
        string buf_trimmed = trim(input_buffer);

        // 检查语句是否完整
        bool complete = false;
        if (!buf_trimmed.empty()) {
            if (buf_trimmed.back() == ';') complete = true;
            if (buf_trimmed.size() >= 2 && buf_trimmed.front() == '$' && buf_trimmed.back() == '$') complete = true;
        }

        if (complete) {
            // 快照当前变量状态
            map<string, Value> saved_vars = variables;

            try {
                // 分割并执行
                istringstream stream(input_buffer);
                string bline;
                string stmt_accum;
                bool cont = true;
                while (getline(stream, bline) && cont) {
                    string tb = trim(bline);
                    if (tb.empty()) continue;
                    stmt_accum += tb + " ";
                    if (tb.back() == ';' || (tb.size() >= 2 && tb.front() == '$' && tb.back() == '$')) {
                        cont = execute_statement(stmt_accum);
                        stmt_accum.clear();
                        if (!cont) break;
                    }
                }
                if (!stmt_accum.empty() && cont) {
                    execute_statement(stmt_accum);
                }
            } catch (const runtime_error& e) {
                // 出错时回滚变量，不清除输入缓冲区（用户可以修正）
                variables = saved_vars;
                cerr << "Statement failed. You can edit and retry, or type a new command." << endl;
                // 不清空 input_buffer，让用户继续编辑
                continue;
            }
            input_buffer.clear();
        }
    }

    if (traceLogEnabled && traceLogFile.is_open()) {
        log_event("REPL session finished");
        traceLogFile.close();
    }
}

// ============================================================
//  主函数
// ============================================================
int main(int argc, char* argv[]) {
    setlocale(LC_ALL, "");
    SetProcessDPIAware();

    if (argc < 2) {
        // ---- REPL 交互模式 ----
        currentScriptDir = filesystem::current_path().string() + "\\";
        repl_mode();
        return 0;
    }

    // ---- 文件模式 ----
    string filePath = argv[1];
    currentScriptDir = filePath.substr(0, filePath.find_last_of("/\\") + 1);
    ifstream infile(filePath);
    if (!infile.is_open()) {
        python_error("cannot open script file '" + filePath + "': file not found or access denied");
        return 1;
    }

    vector<string> lines;
    string line;
    while (getline(infile, line)) {
        lines.push_back(line);
    }
    infile.close();

    // ---- 处理开头的全局指令行 ----
    size_t line_index = 0;
    for (; line_index < lines.size(); ++line_index) {
        string trimmed = trim(lines[line_index]);
        if (trimmed == "$fullScreen on$")   { fullscreen_enabled = true;  continue; }
        if (trimmed == "$fullScreen off$")  { fullscreen_enabled = false; continue; }
        if (trimmed == "$traceLog on$")     { traceLogEnabled = true;     continue; }
        if (trimmed == "$debugMessage on$") { debugMessageEnabled = true; continue; }
        break;
    }

    // ---- 创建日志文件 ----
    if (traceLogEnabled) {
        filesystem::create_directories("TraceLog");
        string filename = "TraceLog/" + get_current_timestamp() + "-TraceLog.txt";
        traceLogFile.open(filename);
        if (!traceLogFile.is_open()) {
            python_error("cannot create trace log file '" + filename + "': check directory permissions");
        } else {
            log_event("script execution started");
        }
    }

    // ---- 全屏 ----
    if (fullscreen_enabled) {
        fullscreen_console();
        hide_taskbar();
    }

    // ---- 执行脚本 ----
    bool continue_flag = execute_program_lines(lines, line_index);

    // ---- 恢复 ----
    if (fullscreen_enabled) {
        restore_console_window();
    }

    if (continue_flag) {
        system("pause");
    }

    if (traceLogEnabled && traceLogFile.is_open()) {
        log_event("script execution finished");
        traceLogFile.close();
    }

    return 0;
}