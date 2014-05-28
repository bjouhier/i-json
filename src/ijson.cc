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

    void intern(char* p, size_t len, Local<Value>* val, Cache* next, uint64_t hash) {
      if (len > CacheEntryMaxSize) {
        *val = next ? String::NewSymbol(p, len) : String::New(p, len);
        return;
      }
      if (hash == 0) hash = fasthash64(p, len, 0);

      CacheEntry* entry = this->entries + (hash % this->size);
      if ((size_t)entry->len == len && !memcmp(p, entry->bytes, len)) {
        *val = entry->value;
        this->hits++;
        return;
      }
      *val = next ? String::NewSymbol(p, len) : String::New(p, len);
      entry->value = *val;
      memcpy(entry->bytes, p, len);
      entry->len = len;
      this->misses++;
    }
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
    std::string* error;
    bool debug;
    State state;
    Frame* frame;
    char* data;
    int len;
    Isolate* isolate;
    std::vector<char> keep;
    Cache* keysCache;
    Cache* valuesCache;

    static uni::CallbackType Update(const uni::FunctionCallbackInfo& args);
    static uni::CallbackType Result(const uni::FunctionCallbackInfo& args);
  };

  class Frame {
  public:
    Frame(Frame* prev, bool alloc) {
      this->prev = prev;
      if (prev) prev->next = this;
      this->next = NULL;
      this->value = alloc ? new Local<Value>() : NULL;
      this->key = alloc ? new Local<Value>() : NULL;
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
    Frame* prev;
    Frame* next;
    bool isArray;
    bool needsValue;

    void setValue(Local<Value> val) {
      //console.log("setValue: key=" + this.key + ", value=" + val);
      if (this->isArray) {
        Local<Array> arr = Local<Array>::Cast(*this->value);
        arr->Set(arr->Length(), val);
      } else {
        Local<Object> obj = Local<Object>::Cast(*this->value);
        obj->Set(*this->key, val);
      }
      this->needsValue = false;
    }
  };

  static int lastClass = 0;

  static int classes[256];

  static int init1() {
    for (int i = 0; i < 256; i++) classes[i] = -1;
    return 0;
  }

  static int dummy1 = init1();

  static int makeClass(const char* str) {
    for (int i = 0, len = strlen(str); i < len; i++) {
      char ch = str[i];
      if (classes[(int)ch] != -1) { printf("duplicate class: %d\n", str[i]); exit(1); }
      classes[(int)ch] = lastClass;
    }
    return lastClass++;
  }

  // basic classes
  static int CURLY_OPEN = makeClass("{"), 
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


  static int init2() {
    for (int i = 0; i < 256; i++) {
      if (classes[i] == -1) classes[i] = lastClass;
    }
    return 0;
  }
  static int dummy2 = init2();

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

  static State BEFORE_VALUE,
    AFTER_VALUE,
    BEFORE_KEY,
    AFTER_KEY,
    INSIDE_QUOTES,
    INSIDE_NUMBER,
    INSIDE_DOUBLE,
    INSIDE_EXP,
    AFTER_ESCAPE,
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

  static void setError(Parser* parser, int pos, const char* detail) {
    char message[80];
    int len = parser->len - pos;
    if (len > 20) len = 20;
    std::string near(parser->data + pos, pos + len);
    std::replace(near.begin(), near.end(), '\n', '\0'); // 
    snprintf(message, sizeof message, "line: %d %s near %s", parser->line, detail, near.c_str());
    parser->error = new std::string(message);
  }

  static void inline error(Parser* parser, int pos, int cla) {
    setError(parser, pos, "syntax error");
  }

  static void inline escapeOpen(Parser* parser, int pos, int cla) {
    parser->keep.insert(parser->keep.end(), parser->data + parser->beg, parser->data + pos);
    parser->beg = -1;
    parser->state = AFTER_ESCAPE;
  }

  static void inline numberOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = false;
    parser->beg = pos;
    parser->state = INSIDE_NUMBER;
  }

  static void inline doubleOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = true;
    parser->state = INSIDE_DOUBLE;
  }

  static void inline expOpen(Parser* parser, int pos, int cla) {
    parser->isDouble = true;
    parser->state = INSIDE_EXP;
  }

  static void inline numberClose(Parser* parser, int pos, int cla) {
    int beg = parser->beg;
    parser->beg = -1;
    char* p = parser->data + beg;
    if (parser->keep.size() != 0) {
      parser->keep.insert(parser->keep.end(), p, parser->data + pos + 1); // append stop byte
      p = &parser->keep[0];
    }
    if (parser->isDouble) {
      parser->frame->setValue(Number::New(atof(p)));
    } else {
      parser->frame->setValue(Integer::New(atoi(p)));      
    }
    parser->keep.clear();
    parseFn fn = AFTER_VALUE[cla];
    if (fn) fn(parser, pos, cla);
    else parser->state = AFTER_VALUE;
  }

  static void inline stringOpen(Parser* parser, int pos, int cla) {
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline stringClose(Parser* parser, int pos, int cla) {
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
      parser->keysCache->intern(p, len, frame->key, parser->valuesCache, 0);
      parser->needsKey = false;
      parser->state = AFTER_KEY;
    } else {
      Local<Value> val;
      parser->valuesCache->intern(p, len, &val, NULL, 0);
      frame->setValue(val);
      parser->state = AFTER_VALUE;
    }
    parser->keep.clear();
  }

  static void inline escapeR(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\r');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline escapeN(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\n');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline escapeT(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\t');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline escapeDQUOTE(Parser* parser, int pos, int cla) {
    parser->keep.push_back('"');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline escapeBSLASH(Parser* parser, int pos, int cla) {
    parser->keep.push_back('\\');
    parser->beg = pos + 1;
    parser->state = INSIDE_QUOTES;
  }

  static void inline t_rue(Parser* parser, int pos, int cla) {
    parser->state = T_RUE;
  }

  static void inline tr_ue(Parser* parser, int pos, int cla) {
    parser->state = TR_UE;
  }

  static void inline tru_e(Parser* parser, int pos, int cla) {
    parser->state = TRU_E;
  }

  static void inline true_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(Local<Value>::New(True()));
    parser->state = AFTER_VALUE;
  }

  static void inline f_alse(Parser* parser, int pos, int cla) {
    parser->state = F_ALSE;
  }

  static void inline fa_lse(Parser* parser, int pos, int cla) {
    parser->state = FA_LSE;
  }

  static void inline fal_se(Parser* parser, int pos, int cla) {
    parser->state = FAL_SE;
  }

  static void inline fals_e(Parser* parser, int pos, int cla) {
    parser->state = FALS_E;
  }

  static void inline false_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(Local<Value>::New(False()));
    parser->state = AFTER_VALUE;
  }

  static void inline n_ull(Parser* parser, int pos, int cla) {
    parser->state = N_ULL;
  }

  static void inline nu_ll(Parser* parser, int pos, int cla) {
    parser->state = NU_LL;
  }

  static void inline nul_l(Parser* parser, int pos, int cla) {
    parser->state = NUL_L;
  }

  static void inline null_(Parser* parser, int pos, int cla) {
    parser->frame->setValue(Local<Value>::New(Null()));
    parser->state = AFTER_VALUE;
  }

  static void inline arrayOpen(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame->next;
    if (frame == NULL) frame = new Frame(parser->frame, true);
    parser->frame = frame;
    frame->isArray = true;
    *frame->value = Array::New(0);
    frame->needsValue = false;
    parser->needsKey = false;
    parser->state = BEFORE_VALUE;
  }

  static void inline arrayClose(Parser* parser, int pos, int cla) {
    if (parser->frame->needsValue) return setError(parser, pos, "expected value after comma, got ]");
    Local<Value> val = *parser->frame->value;
    parser->frame = parser->frame->prev;
    if (parser->frame == NULL) return setError(parser, pos, "too many ]");
    parser->frame->setValue(val);
    parser->state = AFTER_VALUE;
  }

  static void inline objectOpen(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame->next;
    if (frame == NULL) frame = new Frame(parser->frame, true);
    parser->frame = frame;
    frame->isArray = false;
    *frame->value = Object::New();
    frame->needsValue = false;
    parser->needsKey = true;
    parser->state = BEFORE_KEY;
  }

  static void inline objectClose(Parser* parser, int pos, int cla) {
    if (parser->frame->isArray) return setError(parser, pos, "expected ] or comma, got }");
    if (parser->frame->needsValue) return setError(parser, pos, "expected key after comma, got }");
    Local<Value> val = *parser->frame->value;
    parser->frame = parser->frame->prev;
    if (parser->frame == NULL) return setError(parser, pos, "too many }");
    parser->frame->setValue(val);
    parser->state = AFTER_VALUE;
  }

  static void inline eatColon(Parser* parser, int pos, int cla) {
    parser->state = BEFORE_VALUE;
  }

  static void inline eatComma(Parser* parser, int pos, int cla) {
    Frame* frame = parser->frame;
    if (frame->isArray) {
      parser->state = BEFORE_VALUE;
    } else {
      parser->state = BEFORE_KEY;
      parser->needsKey = true;
    }
    frame->needsValue = true;
  }

  static void inline eatNL(Parser* parser, int pos, int cla) {
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
      //{ u_, escapeUnicode }, // SEE LATER
      { -1, NULL }
    };
    AFTER_ESCAPE = makeState(AFTER_ESCAPE_TRANSITIONS, error);

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

  static int dummy3 = initStates();

  int parse(Parser* parser, char* buf, int len) {
    int pos = 0;
    while (pos < len && !parser->error) {
      int ch = buf[pos];
      if (parser->debug) { printf("%c\n", ch); fflush(stdout); }
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
    if (args.Length() != 1) UNI_THROW(Exception::Error(String::New("bad arg count")));
    if (!args[0]->IsObject()) UNI_THROW(Exception::Error(String::New("bad arg type")));
    Local<Object> buf = Local<Object>::Cast(args[0]);
    char* data = Buffer::Data(buf);
    int len = (int)Buffer::Length(buf);
    parser->data = data;
    parser->len = len;

    int cacheLen = len / 16;
    if (cacheLen < 2) cacheLen = 2;
    else if (cacheLen > 512) cacheLen = 512;

    parser->keysCache = new Cache(cacheLen);
    parser->valuesCache = new Cache(cacheLen);

    for (Frame* f = parser->frame; f; f = f->prev) {
      f->value = new Local<Value>();
      *f->value = Local<Value>::New(uni::Deref(f->pvalue));
      uni::Dispose(parser->isolate, f->pvalue);
      f->key = new Local<Value>();
      *f->key = Local<Value>::New(uni::Deref(f->pkey));
      uni::Dispose(parser->isolate, f->pkey);
    }
    int pos = parse(parser, data, len);
    for (Frame* f = parser->frame; f; f = f->prev) {
      uni::Reset(f->pvalue, *f->value);
      delete f->value;
      f->value = NULL;
      uni::Reset(f->pkey, *f->key);
      delete f->key;
      f->key = NULL;
    }
    if (parser->frame) {
      delete parser->frame->next;
      parser->frame->next = NULL;
    }

    delete parser->keysCache;
    delete parser->valuesCache;

    if (parser->error) {
      UNI_THROW(Exception::Error(String::New(parser->error->c_str())));
    }
    if (parser->beg != -1) {
      parser->keep.insert(parser->keep.end(), parser->data + parser->beg, parser->data + pos);
      parser->beg = 0;
    }

    parser->data = NULL;

    UNI_RETURN(scope, args, Undefined());
  }

  uni::CallbackType Parser::Result(const uni::FunctionCallbackInfo& args) {
    UNI_SCOPE(scope);
    Parser* parser = ObjectWrap::Unwrap<Parser>(args.This());
    if (args.Length() != 0) UNI_THROW(Exception::Error(String::New("bad arg count")));

    if (parser->frame->prev) UNI_THROW(Exception::Error(String::New("Unexpected end of input text")));
    Local<Array> arr = Local<Array>::Cast(Local<Value>::New(uni::Deref(parser->frame->pvalue)));
    parser->frame->pvalue.Dispose();
    if (arr->Length() > 1) {
      char message[80];
      snprintf(message, sizeof message, "Too many results: %d", arr->Length());
      UNI_THROW(Exception::Error(String::New(message)));
    }
    UNI_RETURN(scope, args, arr->Get(0));
  }

  void Parser::Init(Handle<Object> target) {
    UNI_SCOPE(scope);

    Local<FunctionTemplate> t = FunctionTemplate::New(New);
    uni::Reset(constructorTemplate, t);
    uni::Deref(constructorTemplate)->InstanceTemplate()->SetInternalFieldCount(1);
    uni::Deref(constructorTemplate)->SetClassName(String::NewSymbol("Parser"));
    NODE_SET_PROTOTYPE_METHOD(uni::Deref(constructorTemplate), "update", Update);
    NODE_SET_PROTOTYPE_METHOD(uni::Deref(constructorTemplate), "result", Result);
    target->Set(String::NewSymbol("Parser"), uni::Deref(constructorTemplate)->GetFunction());
  }

  uni::CallbackType Parser::New(const uni::FunctionCallbackInfo & args) {
    UNI_SCOPE(scope);
    Parser* parser = new Parser();
    parser->Wrap(args.This());
    UNI_RETURN(scope, args, args.This());
  }

  Parser::Parser() {
    this->isolate = Isolate::GetCurrent();
    this->beg = -1;
    this->line = 1;
    this->isDouble = false;
    this->needsKey = false;
    this->error = NULL;
    this->debug = false;
    this->state = BEFORE_VALUE;
    this->frame = new Frame(NULL, false);
    this->frame->isArray = true;
    uni::Reset(this->frame->pvalue, Local<Value>::New(Array::New(0)));
  }

  Parser::~Parser() {
    if (this->error) delete this->error;
    if (this->frame) delete this->frame;
  }
}

extern "C" {
  static void init(Handle<Object> target) {
    ijson::Parser::Init(target);
  }
}

NODE_MODULE(ijson_native, init);