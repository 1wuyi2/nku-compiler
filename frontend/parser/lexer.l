%{
    #include <frontend/parser/yacc.h>
    #include <frontend/parser/scanner.h>
    #include <frontend/parser/location.hh>
    #include <frontend/parser/parser.h>
    #include <stdexcept>
    #include <string>
    #include <cctype>
    #include <cmath>
    #include <limits>

    using namespace FE;

    static location loc;

    // YY_USER_ACTION 宏会自动插入到每个规则的代码块开头
    // 如下方的 "int"               { RETT(INT, loc) }
    // 实际上会被转换为
    // "int"               { loc.step(); loc.columns(yyleng); RETT(INT, loc) }
    // 这样就能自动更新 loc 的位置
    // yyleng 是当前匹配的字符串的长度
    #define YY_USER_ACTION      \
        loc.step();             \
        loc.columns(yyleng);

    #define yywrap() 1
    #define RETT(x, y) \
        return YaccParser::make_##x(y);

    #define TAB_WIDTH 4

    int handleTab();
    long long convertToInt(const char* str, const char end, bool& isLongLong);
    float convertToFloatDec(const char* str);
    float convertToFloatHex(const char* str);
%}

%option c++
%option noyywrap debug
%option yyclass="Scanner"
%option prefix="Yacc_"

DIGIT       [0-9]
HEXDIGIT    [0-9a-fA-F]
DECEXP      [eE][+-]?{DIGIT}+
BINEXP      [pP][+-]?{DIGIT}+
DECFLOAT    (({DIGIT}+\.{DIGIT}*|\.{DIGIT}+){DECEXP}?|{DIGIT}+{DECEXP})
HEXFLOAT    0[xX](({HEXDIGIT}+\.{HEXDIGIT}*|\.{HEXDIGIT}+|{HEXDIGIT}+){BINEXP})

%%

%{
/*
在词法分析中，你需要重点考虑result的四种类型，分别为int_token, float_token, string_token, symbol_token。
之所以要定义这四种类型，是因为我们只返回这个token的类型是不够的，我们还需要知道这个token的属性。
例如我们需要知道INT_CONST具体的值是多少。
如果你需要其它 token，需要先到 yacc.y 中定义 token，然后在这里实现。

TODO(Lab1)：参考SysY2022的定义，实现更多的词法，如浮点数、十六进制/八进制数等
*/
%}

\n              {
                    loc.lines(yyleng);
                    loc.step();
                }

[ \f\r\v]+    { /* empty */ }
[\t]    {
    loc.columns(handleTab());
}

"if"                { RETT(IF, loc) }
"else"              { RETT(ELSE, loc) }
"for"               { RETT(FOR, loc) }
"while"             { RETT(WHILE, loc) }
"continue"          { RETT(CONTINUE, loc) }
"break"             { RETT(BREAK, loc) }
"switch"            { RETT(SWITCH, loc) }
"case"              { RETT(CASE, loc) }
"goto"              { RETT(GOTO, loc) }
"do"                { RETT(DO, loc) }
"return"            { RETT(RETURN, loc) }

"const"             { RETT(CONST, loc) }

";"                 { RETT(SEMICOLON, loc) }
","                 { RETT(COMMA, loc) }

"//".*              { 
    auto str = std::string(yytext);
    // return YaccParser::make_SLASH_COMMENT(str, loc);  //思考为什么要把这一行注释掉
}

"/*""*/" {
    //TODO(Lab1)：实现多行注释的处理，包括修改上方的匹配规则
}

[A-Za-z_]+[A-Za-z0-9_]* {
    auto str = std::string(yytext);
    return YaccParser::make_IDENT(str, loc); 
}

\"[^\"]*\"          { 
    std::string str(yytext);
    if (!str.empty()) str = str.substr(1, str.size() - 2);
    return YaccParser::make_STR_CONST(str, loc); 
}

{HEXFLOAT} {
    try {
        float result = convertToFloatHex(yytext);
        return YaccParser::make_FLOAT_CONST(result, loc);
    } catch (const std::exception& e) {
        _parser.reportError(loc, std::string("Error parsing hexadecimal float: ") + e.what());
        return YaccParser::make_ERR_TOKEN(std::string(yytext), loc);
    }
}

{DECFLOAT} {
    try {
        float result = convertToFloatDec(yytext);
        return YaccParser::make_FLOAT_CONST(result, loc);
    } catch (const std::exception& e) {
        _parser.reportError(loc, std::string("Error parsing decimal float: ") + e.what());
        return YaccParser::make_ERR_TOKEN(std::string(yytext), loc);
    }
}

0[xX]{HEXDIGIT}+|0[0-7]*|[1-9]{DIGIT}* {
    try {
        bool isLL = false;
        long long result = convertToInt(yytext, '\0', isLL);
        if (isLL) return YaccParser::make_LL_CONST(result, loc);
        return YaccParser::make_INT_CONST(static_cast<int>(result), loc);
    } catch (const std::exception& e) {
        _parser.reportError(loc, std::string("Error parsing integer: ") + e.what());
        auto str = std::string(yytext);
        return YaccParser::make_ERR_TOKEN(str, loc);
    }
}

"("                 { RETT(LPAREN, loc) }
")"                 { RETT(RPAREN, loc) }
"["                 { RETT(LBRACKET, loc) }
"]"                 { RETT(RBRACKET, loc) }
"{"                 { RETT(LBRACE, loc) }
"}"                 { RETT(RBRACE, loc) }

<<EOF>>         { RETT(END, loc); }

%%

int handleTab()
{
    // 该函数用于处理制表符，根据当前位置计算制表符应占用的列数
    // 如，在TAB_WIDTH为4的情况下：
    // 对于" \t"，则会对 '\t' 返回 3
    // 对于"   \t"，则会对 '\t' 返回 1
    // 对于"    \t"，则会对 '\t' 返回 4

    int& c = loc.begin.column;
    return TAB_WIDTH - ((c - 1) % TAB_WIDTH) - 1;
}

long long convertToInt(const char* str, const char end, bool& isLongLong)
{
    // 该函数用于将字符串转换为整数，能正确处理格式无误的十进制、十六进制和八进制整数
    // 接受一个 isLongLong 引用参数，用于指示结果是否超出 int 范围

    if (str == nullptr) throw std::invalid_argument("null integer literal");

    int         base       = 10;
    bool        isNegative = false;
    const char* ptr        = str;
    isLongLong             = false;

    if (*ptr == '-' || *ptr == '+')
    {
        isNegative = (*ptr == '-');
        ++ptr;
    }
    if (*ptr == end) throw std::invalid_argument(std::string(str) + " is not a valid integer literal");

    if (ptr[0] == '0' && (ptr[1] == 'x' || ptr[1] == 'X'))
    {
        base = 16;
        ptr += 2;
    }
    else if (ptr[0] == '0')
    {
        base = 8;
    }

    const unsigned long long limit = isNegative
                                         ? static_cast<unsigned long long>(std::numeric_limits<long long>::max()) + 1ULL
                                         : static_cast<unsigned long long>(std::numeric_limits<long long>::max());
    unsigned long long result = 0;
    bool               hasDigit = false;

    while (*ptr != end)
    {
        int value = -1;
        if (*ptr >= '0' && *ptr <= '9') value = *ptr - '0';
        else if (*ptr >= 'a' && *ptr <= 'f') value = *ptr - 'a' + 10;
        else if (*ptr >= 'A' && *ptr <= 'F') value = *ptr - 'A' + 10;

        if (value < 0 || value >= base)
            throw std::invalid_argument(std::string(str) + " is not a valid integer literal");
        if (result > (limit - static_cast<unsigned long long>(value)) / base)
            throw std::out_of_range(std::string(str) + " overflow or underflow for long long");

        result = result * base + value;
        hasDigit = true;
        ++ptr;
    }

    if (!hasDigit) throw std::invalid_argument(std::string(str) + " is not a valid integer literal");

    long long signedResult;
    if (isNegative && result == limit)
        signedResult = std::numeric_limits<long long>::min();
    else
        signedResult = isNegative ? -static_cast<long long>(result) : static_cast<long long>(result);

    isLongLong = signedResult > std::numeric_limits<int>::max() || signedResult < std::numeric_limits<int>::min();
    return signedResult;
}

float convertToFloatDec(const char* str)
{
    // 该函数用于将字符串转换为浮点数，能正确处理格式无误的十进制浮点数

    const char* head = str;
    if (str == NULL) { throw std::invalid_argument("null decimal float literal"); }

    int sign = 1;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+') { str++; }

    double integerPart = 0.0;
    bool   hasDigit    = false;
    while (isdigit(*str))
    {
        integerPart = integerPart * 10 + (*str - '0');
        hasDigit = true;
        str++;
    }

    double fractionPart = 0.0;
    bool   hasFloatSyntax = false;
    if (*str == '.')
    {
        hasFloatSyntax = true;
        str++;
        double divisor = 10.0;
        while (isdigit(*str))
        {
            fractionPart += (*str - '0') / divisor;
            divisor *= 10.0;
            hasDigit = true;
            str++;
        }
    }

    double value = integerPart + fractionPart;

    if (*str == 'e' || *str == 'E')
    {
        hasFloatSyntax = true;
        str++;
        int expSign = 1;
        if (*str == '-')
        {
            expSign = -1;
            str++;
        }
        else if (*str == '+') { str++; }

        if (!isdigit(*str))
            throw std::invalid_argument(std::string(head) + " is not a valid decimal float literal");

        double exponent = 0.0;
        while (isdigit(*str))
        {
            exponent = exponent * 10 + (*str - '0');
            str++;
        }

        if (value != 0.0) value *= pow(10, expSign * exponent);
    }

    if (!hasDigit || !hasFloatSyntax || *str != '\0')
        throw std::invalid_argument(std::string(head) + " is not a valid decimal float literal");

    value = sign * value;

    if (!std::isfinite(value) || value > std::numeric_limits<float>::max() ||
        value < -std::numeric_limits<float>::max())
        throw std::out_of_range(std::string(head) + " is outside the finite float range");

    return static_cast<float>(value);
}

float convertToFloatHex(const char* str)
{
    // 该函数用于将字符串转换为浮点数，能正确处理格式无误的十六进制浮点数

    const char* head = str;
    if (str == NULL) { throw std::invalid_argument("null hexadecimal float literal"); }

    int sign = 1;
    if (*str == '-')
    {
        sign = -1;
        str++;
    }
    else if (*str == '+') { str++; }

    if (*str == '0' && (*(str + 1) == 'x' || *(str + 1) == 'X'))
        str += 2;
    else
        throw std::invalid_argument(std::string(head) + " is not a valid hexadecimal float literal");

    double integerPart = 0.0;
    bool   hasDigit    = false;
    while (isxdigit(*str))
    {
        int digit = 0;
        if (isdigit(*str)) { digit = (*str - '0'); }
        else if (*str >= 'a' && *str <= 'f') { digit = (*str - 'a' + 10); }
        else if (*str >= 'A' && *str <= 'F') { digit = (*str - 'A' + 10); }
        integerPart = integerPart * 16 + digit;
        hasDigit = true;
        str++;
    }

    double fractionPart = 0.0;
    if (*str == '.')
    {
        str++;
        double divisor = 16.0;
        while (isxdigit(*str))
        {
            int digit = 0;
            if (isdigit(*str)) { digit = (*str - '0'); }
            else if (*str >= 'a' && *str <= 'f') { digit = (*str - 'a' + 10); }
            else if (*str >= 'A' && *str <= 'F') { digit = (*str - 'A' + 10); }
            fractionPart += digit / divisor;
            divisor *= 16.0;
            hasDigit = true;
            str++;
        }
    }

    double value       = integerPart + fractionPart;
    bool   hasExponent = false;
    if (*str == 'p' || *str == 'P')
    {
        hasExponent = true;
        str++;
        int expSign = 1;
        if (*str == '-')
        {
            expSign = -1;
            str++;
        }
        else if (*str == '+') { str++; }

        if (!isdigit(*str))
            throw std::invalid_argument(std::string(head) + " is not a valid hexadecimal float literal");

        double exponent = 0.0;
        while (isdigit(*str))
        {
            exponent = exponent * 10 + (*str - '0');
            str++;
        }

        if (value != 0.0) value *= pow(2, expSign * exponent);
    }

    if (!hasDigit || !hasExponent || *str != '\0')
        throw std::invalid_argument(std::string(head) + " is not a valid hexadecimal float literal");

    value = sign * value;

    if (!std::isfinite(value) || value > std::numeric_limits<float>::max() ||
        value < -std::numeric_limits<float>::max())
        throw std::out_of_range(std::string(head) + " is outside the finite float range");

    return static_cast<float>(value);
}
