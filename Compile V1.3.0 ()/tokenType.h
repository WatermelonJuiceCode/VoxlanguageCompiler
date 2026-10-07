#ifndef TOKEN_TYPE_H
#define TOKEN_TYPE_H

enum TokenType{
    Number,
    FloatLiteral, 
    Plus, PlusPlus, PlusEqual,
    Sub, SubSub, SubEqual,
    Mul, MulEqual,
    Div, DivEqual,
    Mod, ModEqual,
    Less, Greater, LessEqual, GreaterEqual,
    EqualEqual, NotEqual,
    Amp,                              // 引用参数 &  (参数里)
    AmpAmp,                           // 逻辑与 &&
    Pipe,                             // 位或 |
    PipePipe,                         // 逻辑或 ||
    Caret,                            // 位异或 ^
    Tilde,                            // 位取反 ~
    Shl, Shr,                         // 位移 << >>
    Bang,                             // 逻辑非 !
    Question,                         // 三元 ?
    At, 
    LParen, RParen,
    LBrace, RBrace,
    LBracket, RBracket,
    END,

    Identifier,
    Equal, Semicolon, Comma, Dot, Colon,
    Arrow, 
    ArrowRight, 
    Using, Comment,

    KwInt, KwString, KwBool, KwChar, KwVoid, KwReturn, KwConst, KwRef, KwRaise,
    KwAny,
    KwInt8, KwInt16, KwInt32, KwInt64,
    KwFloat, KwDouble, 
    KwUInt8, KwUInt16, KwUInt32, KwUInt64,
    KwStruct, 
    KwTemplate, KwTypename, 
    KwClass, KwNew, KwSelf, KwStatic, KwOverride,
    KwSuper,
    KwInterface, 
    KwAbstract, 
    KwPublic, KwPrivate, KwProtected,
    KwFor, KwIf, KwElse, KwWhile, KwDo,
    KwForeach, KwIn,
    KwIs, KwTypeof, KwEnum, 
    KwNamespace, KwExtern,
    KwList, KwDict, KwFinalList, KwFinalDict,
    KwTry, KwCatch, KwFinally,
    KwBreak, KwContinue,
    KwSwitch, KwCase, KwDefault,
    KwWith,

    True, False,
    Null, 
    StringLiteral, CharLiteral
};

#endif