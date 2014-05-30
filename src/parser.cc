/**
 * Copyright (c) 2014 Bruno Jouhier <bjouhier@gmail.com>
 * MIT License
 */
#include <v8.h> 
#include <node.h> 
#include <node_buffer.h>
#include <string>
#include <vector>
#include <algorithm>

using namespace node;
using namespace v8;

#include "uni.h"

namespace ijson {
  class Parser;
  class Frame;
  typedef void (*parseFn)(Parser*, int, int);
  typedef parseFn* State;

#include "fasthash.c"

#define CacheEntryMaxSize 16

  class CacheEntry {
  public:
    CacheEntry() {
      this->len = -1;
    }

    char bytes[CacheEntryMaxSize];
    char len;
    Local<Value> value;
  };

  class Cache {
  public:
    Cache(int size) {
      this->entries = new CacheEntry[size]();
      this->size = size;
      this->hits = 0;
      this->misses = 0;
    }
    ~Cache() {
      //printf("hits=%d, misses=%d\n", this->hits, this->misses);
      delete[] this->entries;
    }
    CacheEntry *entries;
    int size;
    int hits;
    int misses;

    void intern(Parser* parser, char* p, size_t len, Local<Value>* val, Cache* next, uint64_t hash);
  };

  class Parser: public ObjectWrap {
  public: 
    static void Init(Handle<Object> target);
    static uni::CallbackType New(const uni::FunctionCallbackInfo& args);
    static Persistent<FunctionTemplate> constructorTemplate;

    Parser();
    ~Parser();

    int beg;
    int line;
    bool isDouble;
    bool needsKey;
    uint unicode;
    std::string* error;
    State state;
    Frame* frame;
    char* data;
    int len;
    Isolate* isolate;
    std::vector<char> keep;
    Cache* keysCache;
    Cache* valuesCache;
    int callbackDepth;
    Persistent<Function> callback;

    static uni::CallbackType Update(const uni::FunctionCallbackInfo& args);
    static uni::CallbackType Result(const uni::FunctionCallbackInfo& args);
  };

  void Cache::intern(Parser* parser, char* p, size_t len, Local<Value>* val, Cache* next, uint64_t hash) {
    if (len > CacheEntryMaxSize) {
      *val = next ? uni::NewSymbol(parser->isolate, p, len) : uni::NewString(parser->isolate, p, len);
      return;
    }
    if (hash == 0) hash = fasthash64(p, len, 0);

    CacheEntry* entry = this->entries + (hash % this->size);
    if ((size_t)entry->len == len && !memcmp(p, entry->bytes, len)) {
      *val = entry->value;
      this->hits++;
      return;
    }
    *val = next ? uni::NewSymbol(parser->isolate, p, len) : uni::NewString(parser->isolate, p, len);
    entry->value = *val;
    memcpy(entry->bytes, p, len);
    entry->len = len;
    this->misses++;
  }

  class Frame {
  public:
    Frame(Parser* parser, Frame* prev, bool alloc) {
      this->parser = parser;
      this->prev = prev;
      if (prev) prev->next = this;
      this->next = NULL;
      this->value = alloc ? new Local<Value>() : NULL;
      this->key = alloc ? new Local<Value>() : NULL;
      this->depth = prev ? prev->depth + 1 : 0;
    }
    ~Frame() {
      if (this->next) delete this->next;
      if (this->value) delete this->value;
      if (this->key) delete this->key;
    }
    // Local values are faster but we cannot keep them across calls.
    // So we back them with persistent slots.
    Persistent<Value> pvalue;
    Persistent<Value> pkey;
    Local<Value>* value;
    Local<Value>* key;

    Parser* parser;
    Frame* prev;
    Frame* next;
    int arrayPos;
    bool needsValue;
    int depth;

    void setValue(Local<Value> val) {
      this->needsValue = false;
      if (this->depth <= this->parser->callbackDepth) {
        val = this->callback(val);
        if (val->IsUndefined()) {
          if (this->arrayPos >= 0) this->arrayPos++;
          return;
        }
      }
      //console.log("setValue: key=" + this.key + ", value=" + val);
      if (this->arrayPos >= 0) {
        Local<Array> arr = Local<Array>::Cast(*this->value);
        arr->Set(arr->Length(), val);
        this->arrayPos++;
      } else {
        Local<Object> obj = Local<Object>::Cast(*this->value);
        obj->Set(*this->key, val);
      }
    }

    Local<Value> callback(Local<Value> val) {
      Isolate* isolate = this->parser->isolate;
      Local<Array> path = uni::NewArray(isolate, this->depth);
      for (Frame* f = this; f->prev; f = f->prev) {
        if (f->arrayPos >= 0) path->Set(f->depth - 1, uni::NewInteger(isolate, f->arrayPos));
        else path->Set(f->depth - 1, *f->key);
      }
      Handle<Value> argv[2];
      argv[0] = val;
      argv[1] = path;
      Handle<Value> res = uni::MakeCallback(isolate, 
        uni::GetCurrentContext(isolate)->Global(), 
        uni::Deref(isolate, this->parser->callback), 2, argv);
      return uni::HandleToLocal(res);
    }

    void restore(Isolate* isolate) {
      this->value = new Local<Value>();
      *this->value = uni::HandleToLocal(uni::Deref(isolate, this->pvalue));
      uni::Dispose(isolate, this->pvalue);
      this->key = new Local<Value>();
      *this->key = uni::HandleToLocal(uni::Deref(isolate, this->pkey));
      uni::Dispose(isolate, this->pkey);
    }

    void save(Isolate* isolate) {
      uni::Reset(isolate, this->pvalue, *this->value);
      delete this->value;
      this->value = NULL;
      uni::Reset(isolate, this->pkey, *this->key);
      delete this->key;
      this->key = NULL;
    }
  };

  int lastClass = 0;

  int classes[256];

  int init1() {
    for (int i = 0; i < 256; i++) classes[i] = -1;
    return 0;
  }

  int dummy1 = init1();

  int makeClass(const char* str) {
    for (int i = 0, len = strlen(str); i < len; i++) {
      char ch = str[i];
      if (classes[(int)ch] != -1) { printf("duplicate class: %d\n", str[i]); exit(1); }
      classes[(int)ch] = lastClass;
    }
    return lastClass++;
  }

  // basic classes
  int CURLY_OPEN = makeClass("{"), 
    CURLY_CLOSE = makeClass("}"),
    SQUARE_OPEN = makeClass("["), 
    SQUARE_CLOSE = makeClass("]"),
    COMMA = makeClass(","),
    COLON = makeClass(":"),
    DQUOTE = makeClass("\""),
    BSLASH = makeClass("\\"),
    SPACE = makeClass(" \t\r"),
    NL = makeClass("\n"),
    t_ = makeClass("t"),
    r_ = makeClass("r"),
    u_ = makeClass("u"),
    e_ = makeClass("e"),
    f_ = makeClass("f"),
    a_ = makeClass("a"),
    l_ = makeClass("l"),
    s_ = makeClass("s"),
    n_ = makeClass("n"),
    PLUS = makeClass("+"),
    MINUS = makeClass("-"),
    DIGIT = makeClass("0123456789"),
    DOT = makeClass("."),
    E_ = makeClass("E"),
    HEX_REMAIN = makeClass("ABCDFbcd");


  int init2() {
    for (int i = 0; i < 256; i++) {
      if (classes[i] == -1) classes[i] = lastClass;
    }
    return 0;
  }
  int dummy2 = init2();

  typedef struct Transition {
    int cla;
    parseFn fn;
  } Transition;

  State makeState(Transition* transitions, parseFn def) {
    State state = new parseFn[lastClass + 1];
    for (int i = 0; i <= lastClass; i++) state[i] = def;
    for (int i = 0; transitions[i].cla != -1; i++) {
      Transition transition = transitions[i];
      state[transition.cla] = transition.fn;
    }
    return state;
  }

  void fillTransition(Transition* t, int cla, parseFn fn) {
    t->cla = cla;
    t->fn = fn;
  }

  inline int hex(char ch) {
    if (ch <= '9') return ch - '0';
    if (ch <= 'F') return 10 + ch - 'A';
    return 10 + ch - 'a';
  }

  State makeHexState(parseFn fn, parseFn def) {
    Transition* transitions = new Transition[7];
    fillTransition(transitions + 0, DIGIT, fn);
    fillTransition(transitions + 1, a_, fn);
    fillTransition(transitions + 2, e_, fn);
    fillTransition(transitions + 3, f_, fn);
    fillTransition(transitions + 4, E_, fn);
    fillTransition(transitions + 5, HEX_REMAIN, fn);
    fillTransition(transitions + 6, -1, NULL);
    return makeState(transitions, def);
  }

  State BEFORE_VALUE,
    AFTER_VALUE,
    BEFORE_KEY,
    AFTER_KEY,
    INSIDE_QUOTES,
    INSIDE_NUMBER,
    INSIDE_DOUBLE,
    INSIDE_EXP,
    AFTER_ESCAPE,
    U_XXXX,
    UX_XXX,
    UXX_XX,
    UXXX_X,
    T_RUE,
    TR_UE,
    TRU_E,
    F_ALSE,
    FA_LSE,
    FAL_SE,
    FALS_E,
    N_ULL,
    NU_LL,
    NUL_L;

  void setError(Parser* parser, int pos) {
    char message[80];
    int len = parser->len - pos;
    if (len > 20) len = 20;
    std::string near(parser->data + pos, len);
    std::replace(near.begin(), near.end(), '\n', '\0'); // 
    snprintf(message, sizeof message, "line %d: syntax error near %s", parser->line, near.c_str());
    parser->error = new std::string(message);
  }

  void inline error(Parser* parser, int pos, int cla) {
    setError(parser, pos);
  }

  void inline numberOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = false;
    parser->beg = pos;
    parser->state = INSIDE_NUMBER;
  }

  void inline doubleOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = true;
    parser->state = INSIDE_DOUBLE;
  }

  void inline expOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = true;
    parser->state = INSIDE_EXP;
  }

  void numberClose(Parser* parser, int pos, int cla) {
    int beg = parser->beg;
    parser->beg = -1;
    char* p = parser->data + beg;
    if (parser->keep.size() != 0) {
      parser->keep.insert(parser->keep.end(), p, parser->data + pos + 1); // append stop byte
      p = &parser->keep[0];
    }
    if (parser->isDouble) {
      parser->frame->setValue(uni::NewNumber(parser->isolate, atof(p)));
    } else {
      parser->frame->setValue(uni::NewInteger(parser->isolate, atoi(p)));      
    }
    parser->keep.clear();
    parseFn fn = AFTER_VALUE[cla];
    if (fn) fn(parser, pos, cla);
    else parser->state = AFTER_VALUE;
  }

  void inline stringOpen(Parser* parser, int pos, int cla) {
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void stringClose(Parser* parser, int pos, int cla) {
    char* p = parser->data + parser->beg;
    size_t len = (size_t)(pos - parser->beg);
    parser->beg = -1;
    if (parser->keep.size() != 0) {
      len += parser->keep.size();
      parser->keep.insert(parser->keep.end(), p, parser->data + pos);
      p = &parser->keep[0];
    }
    Frame* frame = parser->frame;

    if (parser->needsKey) {
      parser->keysCache->intern(parser, p, len, frame->key, parser->valuesCache, 0);
      parser->needsKey = false;
      parser->state = AFTER_KEY;
    } else {
      Local<Value> val;
      parser->valuesCache->intern(parser, p, len, &val, NULL, 0);
      frame->setValue(val);
      parser->state = AFTER_VALUE;
    }
    parser->keep.clear();
  }

  void inline escapeOpen(Parser* parser, int pos, int cla) {
    parser->keep.insert(parser->keep.end(), parser->data + parser->beg, parser->data + pos);
    parser->beg = -1;
    parser->state = AFTER_ESCAPE;
  }

  void inline escapeR(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\r');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline escapeN(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\n');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline escapeT(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\t');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline escapeDQUOTE(Parser* parser, int pos, int cla) {
    parser->keep.push_back('"');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline escapeBSLASH(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\\');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline u_xxxx(Parser* parser, int pos, int cla) {
    parser->state = U_XXXX;
  }

  void inline ux_xxx(Parser* parser, int pos, int cla) {
    parser->unicode = hex(parser->data[pos]);
    parser->state = UX_XXX;
  }

  void inline uxx_xx(Parser* parser, int pos, int cla) {
    parser->unicode = parser->unicode * 16 + hex(parser->data[pos]);
    parser->state = UXX_XX;
  }

  void inline uxxx_x(Parser* parser, int pos, int cla) {
    parser->unicode = parser->unicode * 16 + hex(parser->data[pos]);
    parser->state = UXXX_X;
  }

  void uxxxx_(Parser* parser, int pos, int cla) {
    uint u = parser->unicode * 16 + hex(parser->data[pos]);
    // push UTF-8 representation of u
    if (u < 0x80) parser->keep.push_back((char)u);
    else if (u < 0x0800) {
      parser->keep.push_back(0xc0 + (u >> 6));
      parser->keep.push_back(0x80 + (u & 0x3f));
    } else {
      parser->keep.push_back(0xe0 + (u >> 12));
      parser->keep.push_back(0x80 + ((u >> 6) & 0x3f));
      parser->keep.push_back(0x80 + (u & 0x3f));
    }
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  void inline t_rue(Parser* parser, int pos, int cla) {
    parser->state = T_RUE;
  }

  void inline tr_ue(Parser* parser, int pos, int cla) {
    parser->state = TR_UE;
  }

  void inline tru_e(Parser* parser, int pos, int cla) {
    parser->state = TRU_E;
  }

  void inline true_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(uni::HandleToLocal(uni::True(parser->isolate)));
    parser->state = AFTER_VALUE;
  }

  void inline f_alse(Parser* parser, int pos, int cla) {
    parser->state = F_ALSE;
  }

  void inline fa_lse(Parser* parser, int pos, int cla) {
    parser->state = FA_LSE;
  }

  void inline fal_se(Parser* parser, int pos, int cla) {
    parser->state = FAL_SE;
  }

  void inline fals_e(Parser* parser, int pos, int cla) {
    parser->state = FALS_E;
  }

  void inline false_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(uni::HandleToLocal(uni::False(parser->isolate)));
    parser->state = AFTER_VALUE;
  }

  void inline n_ull(Parser* parser, int pos, int cla) {
    parser->state = N_ULL;
  }

  void inline nu_ll(Parser* parser, int pos, int cla) {
    parser->state = NU_LL;
  }

  void inline nul_l(Parser* parser, int pos, int cla) {
    parser->state = NUL_L;
  }

  void inline null_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(uni::HandleToLocal(uni::Null(parser->isolate)));
    parser->state = AFTER_VALUE;
  }

  void arrayOpen(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame->next;
    if (frame == NULL) frame = new Frame(parser, parser->frame, true);
    parser->frame = frame;
    frame->arrayPos = 0;
    *frame->value = uni::NewArray(parser->isolate, 0);
    frame->needsValue = false;
    parser->needsKey = false;
    parser->state = BEFORE_VALUE;
  }

  void arrayClose(Parser* parser, int pos, int cla) {
    if (parser->frame->arrayPos == -1) return setError(parser, pos);
    if (parser->frame->needsValue) return setError(parser, pos);
    Local<Value> val = *parser->frame->value;
    parser->frame = parser->frame->prev;
    if (parser->frame == NULL) return setError(parser, pos);
    parser->frame->setValue(val);
    parser->state = AFTER_VALUE;
  }

  void objectOpen(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame->next;
    if (frame == NULL) frame = new Frame(parser, parser->frame, true);
    parser->frame = frame;
    frame->arrayPos = -1;
    *frame->value = uni::NewObject(parser->isolate);
    frame->needsValue = false;
    parser->needsKey = true;
    parser->state = BEFORE_KEY;
  }

  void objectClose(Parser* parser, int pos, int cla) {
    if (parser->frame->arrayPos >= 0) return setError(parser, pos);
    if (parser->frame->needsValue) return setError(parser, pos);
    Local<Value> val = *parser->frame->value;
    parser->frame = parser->frame->prev;
    if (parser->frame == NULL) return setError(parser, pos);
    parser->frame->setValue(val);
    parser->state = AFTER_VALUE;
  }

  void inline eatColon(Parser* parser, int pos, int cla) {
    parser->state = BEFORE_VALUE;
  }

  void inline eatComma(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame;
    if (frame->arrayPos >= 0) {
      parser->state = BEFORE_VALUE;
    } else {
      parser->state = BEFORE_KEY;
      parser->needsKey = true;
    }
    frame->needsValue = true;
  }

  void inline eatNL(Parser* parser, int pos, int cla) {
    parser->line++;
  }

  int initStates() {
    Transition BEFORE_VALUE_TRANSITIONS[] = {
      { CURLY_OPEN, objectOpen },
      { SQUARE_OPEN, arrayOpen },
      { DQUOTE, stringOpen },
      { MINUS, numberOpen },
      { DIGIT, numberOpen },
      { t_, t_rue },
      { f_, f_alse },
      { n_, n_ull },
      { SPACE, NULL },
      { NL, eatNL },
      { SQUARE_CLOSE, arrayClose },
      { -1, NULL }
    };
    BEFORE_VALUE = makeState(BEFORE_VALUE_TRANSITIONS, error);

    Transition AFTER_VALUE_TRANSITIONS[] = {
      { COMMA, eatComma },
      { CURLY_CLOSE, objectClose },
      { SQUARE_CLOSE, arrayClose },
      { SPACE, NULL },
      { NL, eatNL },
      { -1, NULL }
    };
    AFTER_VALUE = makeState(AFTER_VALUE_TRANSITIONS, error);

    // object states
    Transition BEFORE_KEY_TRANSITIONS[] = {
      { DQUOTE, stringOpen },
      { CURLY_CLOSE, objectClose },
      { SPACE, NULL },
      { NL, eatNL },
      { -1, NULL }
    };
    BEFORE_KEY = makeState(BEFORE_KEY_TRANSITIONS, error);

    Transition AFTER_KEY_TRANSITIONS[] = {
      { COLON, eatColon },
      { SPACE, NULL },
      { NL, eatNL },
      { -1, NULL }
    };
    AFTER_KEY = makeState(AFTER_KEY_TRANSITIONS, error);

    // string states
    Transition INSIDE_QUOTES_TRANSITIONS[] = {
      { DQUOTE, stringClose },
      { BSLASH, escapeOpen },
      { NL, error },
      { -1, NULL }
    };
    INSIDE_QUOTES = makeState(INSIDE_QUOTES_TRANSITIONS, NULL);

    Transition AFTER_ESCAPE_TRANSITIONS[] = {
      { n_, escapeN },
      { r_, escapeR },
      { t_, escapeT },
      { DQUOTE, escapeDQUOTE },
      { BSLASH, escapeBSLASH },
      { u_, u_xxxx }, 
      { -1, NULL }
    };
    AFTER_ESCAPE = makeState(AFTER_ESCAPE_TRANSITIONS, error);

    U_XXXX = makeHexState(ux_xxx, error);
    UX_XXX = makeHexState(uxx_xx, error);
    UXX_XX = makeHexState(uxxx_x, error);
    UXXX_X = makeHexState(uxxxx_, error);
    
    Transition T_RUE_TRANSITIONS[] = {
      { r_, tr_ue },
      { -1, NULL }
    };
    T_RUE = makeState(T_RUE_TRANSITIONS, error);
    
    Transition TR_UE_TRANSITIONS[] = {
      { u_, tru_e },
      { -1, NULL }
    };
    TR_UE = makeState(TR_UE_TRANSITIONS, error);
    
    Transition TRU_E_TRANSITIONS[] = {
      { e_, true_ },
      { -1, NULL }
    };
    TRU_E = makeState(TRU_E_TRANSITIONS, error);
    
    Transition F_ALSE_TRANSITIONS[] = {
      { a_, fa_lse },
      { -1, NULL }
    };
    F_ALSE = makeState(F_ALSE_TRANSITIONS, error);
    
    Transition FA_LSE_TRANSITIONS[] = {
      { l_, fal_se },
      { -1, NULL }
    };
    FA_LSE = makeState(FA_LSE_TRANSITIONS, error);
    
    Transition FAL_SE_TRANSITIONS[] = {
      { s_, fals_e },
      { -1, NULL }
    };
    FAL_SE = makeState(FAL_SE_TRANSITIONS, error);
    
    Transition FALS_E_TRANSITIONS[] = {
      { e_, false_ },
      { -1, NULL }
    };
    FALS_E = makeState(FALS_E_TRANSITIONS, error);
    
    Transition N_ULL_TRANSITIONS[] = {
      { u_, nu_ll },
      { -1, NULL }
    };
    N_ULL = makeState(N_ULL_TRANSITIONS, error);
    
    Transition NU_LL_TRANSITIONS[] = {
      { l_, nul_l },
      { -1, NULL }
    };
    NU_LL = makeState(NU_LL_TRANSITIONS, error);
    
    Transition NUL_L_TRANSITIONS[] = {
      { l_, null_ },
      { -1, NULL }
    };
    NUL_L = makeState(NUL_L_TRANSITIONS, error);
    
    // number transition - a bit loose, let parse handle errors
    Transition INSIDE_NUMBER_TRANSITIONS[] = {
      { DIGIT, NULL },
      { DOT, doubleOpen },
      { e_, expOpen },
      { E_, expOpen },
      { -1, NULL }
    };
    INSIDE_NUMBER = makeState(INSIDE_NUMBER_TRANSITIONS, numberClose);

    Transition INSIDE_DOUBLE_TRANSITIONS[] = {
      { DIGIT, NULL },
      { e_, expOpen },
      { E_, expOpen },
      { -1, NULL }
    };
    INSIDE_DOUBLE = makeState(INSIDE_DOUBLE_TRANSITIONS, numberClose);


    Transition INSIDE_EXP_TRANSITIONS[] = {
      { PLUS, NULL },
      { MINUS, NULL },
      { DIGIT, NULL },
      { -1, NULL }
    };
    INSIDE_EXP = makeState(INSIDE_EXP_TRANSITIONS, numberClose);

    return 0;
  }

  int dummy3 = initStates();

  int parse(Parser* parser, char* buf, int len) {
    parser->data = buf;
    parser->len = len;
    int pos = 0;
    while (pos < len && !parser->error) {
      int ch = buf[pos];
      int cla = classes[ch];
      parseFn fn = parser->state[cla];
      if (fn != NULL) fn(parser, pos, cla);
      pos++;
    }
    return pos;
  }

  // API
  Persistent<FunctionTemplate> Parser::constructorTemplate;

  uni::CallbackType Parser::Update(const uni::FunctionCallbackInfo& args) {
    UNI_SCOPE(scope);
    Parser* parser = ObjectWrap::Unwrap<Parser>(args.This());
    Isolate* isolate = parser->isolate;
    if (args.Length() < 1) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg count")));
    if (!args[0]->IsObject() || !Buffer::HasInstance(args[0])) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg 1: buffer expected")));
    
    Local<Object> buf = Local<Object>::Cast(args[0]);
    char* data = Buffer::Data(buf);
    int len = (int)Buffer::Length(buf);

    int cacheLen = len / 16;
    if (cacheLen < 2) cacheLen = 2;
    else if (cacheLen > 512) cacheLen = 512;

    parser->keysCache = new Cache(cacheLen);
    parser->valuesCache = new Cache(cacheLen);

    for (Frame* f = parser->frame; f; f = f->prev) f->restore(isolate);

    int pos = parse(parser, data, len);

    for (Frame* f = parser->frame; f; f = f->prev) f->save(isolate);

    if (parser->frame) {
      delete parser->frame->next;
      parser->frame->next = NULL;
    }

    delete parser->keysCache;
    delete parser->valuesCache;

    if (parser->error) {
      UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, parser->error->c_str())));
    }
    if (parser->beg != -1) {
      parser->keep.insert(parser->keep.end(), parser->data + parser->beg, parser->data + pos);
      parser->beg = 0;
    }

    parser->data = NULL;

    UNI_RETURN(scope, args, uni::Undefined(isolate));
  }

  uni::CallbackType Parser::Result(const uni::FunctionCallbackInfo& args) {
    UNI_SCOPE(scope);
    Parser* parser = ObjectWrap::Unwrap<Parser>(args.This());
    Isolate* isolate = parser->isolate;
    if (args.Length() != 0) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg count")));

    if (parser->frame->prev) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "Unexpected end of input")));
    
    // number values are only closed when we read past them. So we parse an extra space if still inside a number.
    if (parser->state == INSIDE_NUMBER || parser->state == INSIDE_DOUBLE || parser->state == INSIDE_EXP) {
      parser->frame->restore(isolate);
      parse(parser, (char*)" ", 1);
      parser->frame->save(isolate);
    }
    if (parser->state != AFTER_VALUE) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "Unexpected end of input")));
    Local<Array> arr = Local<Array>::Cast(uni::HandleToLocal(uni::Deref(isolate, parser->frame->pvalue)));
    uni::Dispose(isolate, parser->frame->pvalue);
    if (arr->Length() > 1) {
      char message[80];
      snprintf(message, sizeof message, "Too many results: %d", arr->Length());
      UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, message)));
    }
    UNI_RETURN(scope, args, arr->Get(0));
  }

  void Parser::Init(Handle<Object> target) {
    UNI_SCOPE(scope);

    Isolate* isolate = Isolate::GetCurrent();
    Local<FunctionTemplate> t = uni::NewFunctionTemplate(isolate, New);
    uni::Reset(isolate, constructorTemplate, t);
    uni::Deref(isolate, constructorTemplate)->InstanceTemplate()->SetInternalFieldCount(1);
    uni::Deref(isolate, constructorTemplate)->SetClassName(uni::NewSymbol(isolate, "Parser"));
    NODE_SET_PROTOTYPE_METHOD(uni::Deref(isolate, constructorTemplate), "_update", Update);
    NODE_SET_PROTOTYPE_METHOD(uni::Deref(isolate, constructorTemplate), "result", Result);
    target->Set(uni::NewSymbol(isolate, "Parser"), uni::Deref(isolate, constructorTemplate)->GetFunction());
  }

  uni::CallbackType Parser::New(const uni::FunctionCallbackInfo & args) {
    UNI_SCOPE(scope);
    Parser* parser = new Parser();
    Isolate* isolate = parser->isolate;
    parser->Wrap(args.This());
    // little js wrapper is responsible for passing 2 args
    if (args.Length() != 2) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg count")));
    if (!args[0]->IsUndefined()) {
      if (!args[0]->IsFunction()) UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg 1: function expected"))); 
      uni::Reset(isolate, parser->callback, Local<Function>::Cast(args[0]));
      if (!args[1]->IsUndefined()) {
        if (!args[1]->IsNumber())  UNI_THROW(isolate, Exception::Error(uni::NewString(isolate, "bad arg 2: integer expected")));
        parser->callbackDepth = args[1]->Int32Value();
      } else {
        parser->callbackDepth = 0x7fffffff;
      }
    }
    UNI_RETURN(scope, args, args.This());
  }

  Parser::Parser() {
    Isolate* isolate =  Isolate::GetCurrent();
    this->isolate = isolate;
    this->beg = -1;
    this->line = 1;
    this->isDouble = false;
    this->needsKey = false;
    this->error = NULL;
    this->state = BEFORE_VALUE;
    this->frame = new Frame(this, NULL, false);
    this->frame->arrayPos = 0;
    uni::Reset(isolate, this->frame->pvalue, uni::NewValue(isolate, uni::NewArray(isolate, 0)));
    this->callbackDepth = -1;
  }

  Parser::~Parser() {
    if (this->error) delete this->error;
    if (this->frame) delete this->frame;
    uni::Dispose(this->isolate, this->callback);
  }
}

extern "C" {
  static void init(Handle<Object> target) {
    ijson::Parser::Init(target);
  }
}

NODE_MODULE(ijson_bindings, init);