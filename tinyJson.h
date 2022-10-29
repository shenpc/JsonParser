
#ifndef TINYJSON_INCLUDED
#define TINYJSON_INCLUDED
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string>

#if defined( _DEBUG ) || defined( DEBUG ) || defined(__DEBUG__)
#ifndef DEBUG
#define DEBUG
#endif
#endif


#if defined(DEBUG)
#if defined(_MSC_VER)
#define TJASSERT( x )           if ( !(x)) { __debugbreak(); } 
#else
#include <assert.h>
#define TJASSERT                assert
#endif
#else
#define TJASSERT( x )           {}
#endif

namespace tinyjson
{
class JsonNumber;
class JsonString;
class JsonElement;
class JsonObject;
class JsonArray;
class JsonDocument;
class JsonNode;
class JsonReserved;


enum class JsonError {
    JSON_NO_ERROR = 0,

    JSON_ERROR_FILE_NOT_FOUND,
    JSON_ERROR_FILE_COULD_NOT_BE_OPENED,
    JSON_ERROR_FILE_READ_ERROR,
    JSON_ERROR_MEM_POOL_ERROR,
    JSON_ERROR_OBJECT_MISMATCH,
    JSON_ERROR_PARSING_OBJECT,
    JSON_ERROR_ARRAY_MISMATCH,
    JSON_ERROR_PARSING_ELEMENT,
    JSON_ERROR_PARSING_NUMBER,
    JSON_ERROR_PARSING_STRING,
    JSON_ERROR_PARSING_RESERVED,
    JSON_ERROR_PARSING,

    JSON_ERROR_EMPTY_DOCUMENT,
};

class JsonUtil
{
public:
    // Anything in the high order range of UTF-8 is assumed to not be whitespace. This isn't
    // correct, but simple, and usually works.
    static const char *SkipWhiteSpace(const char *p)
    {
        if (p == nullptr) {
            return nullptr;
        }
        while (!IsUTF8Continuation(*p) && isspace(*reinterpret_cast<const unsigned char *>(p))) {
            ++p;
        }
        return p;
    }
    static char *SkipWhiteSpace(char *p) 
    {
        if (p == nullptr) {
            return nullptr;
        }
        while (!IsUTF8Continuation(*p) && isspace(*reinterpret_cast<unsigned char *>(p))) {
            ++p;
        }
        return p;
    }
    static bool IsWhiteSpace(char p)
    {
        return !IsUTF8Continuation(p) && isspace(static_cast<unsigned char>(p));
    }

    inline static int IsUTF8Continuation(const char p)
    {
        return p & 0x80;
    }
    inline static int IsAlphaNum(unsigned char anyByte) 
    {
        return (anyByte < 128) ? isalnum(anyByte) : 1;
    }
    inline static int IsAlpha(unsigned char anyByte) 
    {
        return (anyByte < 128) ? isalpha(anyByte) : 1;
    }
};

class StrPair
{
public:
    StrPair() : _start(0), _end(0) {}
    ~StrPair()
    {
        Reset();
    }

    void Set(char *start, char *end) 
    {
        Reset();
        _start = start;
        _end = end;
    }

    const std::string GetStr() const
    {
        std::string str;
        if (_start == nullptr || _end == nullptr || _start == _end) {
            return str;
        }
        str = _start;
        return str;
    }

    bool Empty() const {
        return _start == _end;
    }
private:
    void Reset()
    {
        _start = 0;
        _end = 0;
    }

    char *_start;
    char *_end;
};

template <class T, int INIT>
class DynArray
{
public:
    DynArray< T, INIT >() 
    {
        _mem = _pool;
        _allocated = INIT;
        _size = 0;
    }

    ~DynArray() 
    {
        if (_mem != _pool) {
            delete[] _mem;
            _mem = nullptr;
        }
    }

    void Push(T t) 
    {
        EnsureCapacity(_size + 1);
        _mem[_size++] = t;
    }

    T *PushArr(int count) 
    {
        EnsureCapacity(_size + count);
        T *ret = &_mem[_size];
        _size += count;
        return ret;
    }

    T Pop() 
    {
        return _mem[--_size];
    }

    void PopArr(int count) 
    {
        TJASSERT(_size >= count);
        _size -= count;
    }

    bool Empty() const 
    {
        return _size == 0;
    }

    T &operator[](int i) 
    {
        TJASSERT(i >= 0 && i < _size);
        return _mem[i];
    }

    const T &operator[](int i) const 
    {
        TJASSERT(i >= 0 && i < _size);
        return _mem[i];
    }

    int Size() const 
    {
        return _size;
    }

    int Capacity() const 
    {
        return _allocated;
    }

    const T *Mem() const 
    {
        return _mem;
    }

    T *Mem() 
    {
        return _mem;
    }

private:
    void EnsureCapacity(int cap) 
    {
        if (cap > _allocated) {
            int newAllocated = cap * 2;
            T *newMem = new T[newAllocated];
            memcpy(newMem, _mem, sizeof(T) * _size);
            if (_mem != _pool) {
                delete[] _mem;
            }
            _mem = newMem;
            _allocated = newAllocated;
        }
    }

    T *_mem;
    T _pool[INIT];
    int _allocated;
    int _size;
};


class MemPool
{
public:
    MemPool() {}
    virtual ~MemPool() {}

    virtual int ItemSize() const = 0;
    virtual void *Alloc() = 0;
    virtual void Free(void *) = 0;
    virtual void SetTracked() = 0;
};

template< int SIZE >
class MemPoolT : public MemPool
{
public:
    MemPoolT() : _root(0), _currentAllocs(0), _nAllocs(0), _maxAllocs(0), _nUntracked(0) {}
    ~MemPoolT() 
    {
        for (int i = 0; i < _blockPtrs.Size(); ++i) {
            delete _blockPtrs[i];
        }
    }

    virtual int ItemSize() const 
    {
        return SIZE;
    }

    int CurrentAllocs() const 
    {
        return _currentAllocs;
    }

    virtual void *Alloc() 
    {
        if (_root == nullptr) {
            Block *block = new Block();
            _blockPtrs.Push(block);

            for (int i = 0; i < COUNT - 1; ++i) {
                block->chunk[i].next = &block->chunk[i + 1];
            }
            block->chunk[COUNT - 1].next = nullptr;
            _root = block->chunk;
        }
        void *result = _root;
        _root = _root->next;

        ++_currentAllocs;
        if (_currentAllocs > _maxAllocs) {
            _maxAllocs = _currentAllocs;
        }
        _nAllocs++;
        _nUntracked++;
        return result;
    }

    virtual void Free(void *mem) 
    {
        if (mem == nullptr) {
            return;
        }
        --_currentAllocs;
        Chunk *chunk = (Chunk *)mem;
#ifdef DEBUG
        memset(chunk, 0xfe, sizeof(Chunk));
#endif
        chunk->next = _root;
        _root = chunk;
    }
    void Trace(const char *name) 
    {
        printf("Mempool %s watermark=%d [%dk] current=%d size=%d nAlloc=%d blocks=%d\n",
            name, _maxAllocs, _maxAllocs * SIZE / 1024, _currentAllocs, SIZE, _nAllocs, _blockPtrs.Size());
    }
#ifdef DEBUG
    void SetTracked() 
    {
        _nUntracked--;
    }

    int Untracked() const 
    {
        return _nUntracked;
    }
#endif
    enum { COUNT = 1024 / SIZE }; 

private:
    union Chunk {
        Chunk *next;
        char mem[SIZE];
    };
    struct Block {
        Chunk chunk[COUNT];
    };
    DynArray< Block *, 10 > _blockPtrs;
    Chunk *_root;

    int _currentAllocs;
    int _nAllocs;
    int _maxAllocs;
#ifdef DEBUG
    int _nUntracked;
#endif
};

class JsonVisitor
{
public:
    virtual ~JsonVisitor() {}

    virtual bool VisitEnter(const JsonObject &)
    {
        return true;
    }
    virtual bool VisitExit(const JsonObject &)
    {
        return true;
    }
    virtual bool VisitEnter(const JsonArray &)
    {
        return true;
    }
    virtual bool VisitExit(const JsonArray &)
    {
        return true;
    }
    virtual bool VisitEnter(const JsonElement &)
    {
        return true;
    }
    virtual bool VisitExit(const JsonElement &)
    {
        return true;
    }
    virtual bool Visit(const JsonNumber &)
    {
        return true;
    }

    virtual bool Visit(const JsonString &)
    {
        return true;
    }

    virtual bool Visit(const JsonReserved &)
    {
        return true;
    }
};

class JsonNode
{
    friend JsonDocument;
public:
    static inline void DeleteNode(JsonNode *node)
    {
        if (node != nullptr) {
            MemPool *pool = node->_memPool;
            node->~JsonNode();
            pool->Free(node);
        }
    }

    virtual char *ParseDeep(char *json);
    void DeleteChildren();
    JsonNode *InsertEndChild(JsonNode *addThis);
    const JsonNode *FirstChild() const
    {
        return _firstChild;
    }
    JsonNode *FirstChild()
    {
        return _firstChild;
    }
    const JsonNode *NextSibling() const
    {
        return _next;
    }
    JsonNode *NextSibling()
    {
        return _next;
    }
    const JsonNode *LastChild() const
    {
        return _lastChild;
    }

    JsonNode *LastChild()
    {
        return const_cast<JsonNode *>(const_cast<const JsonNode *>(this)->LastChild());
    }
    const JsonNode *Parent() const
    {
        return _parent;
    }

    JsonNode *Parent()
    {
        return _parent;
    }
    const JsonNode *PreviousSibling() const
    {
        return _prev;
    }

    JsonNode *PreviousSibling() 
    {
        return _prev;
    }

    MemPool *GetMemPool()
    {
        return _memPool;
    }
    virtual JsonElement *ToElement()
    {
        return 0;
    }
    virtual JsonObject *ToObject()
    {
        return 0;
    }
    virtual JsonArray *ToArray()
    {
        return 0;
    }
    virtual const JsonElement *ToElement() const
    {
        return 0;
    }
    virtual const JsonObject *ToObject() const
    {
        return 0;
    }
    virtual const JsonArray *ToArray() const
    {
        return 0;
    }
    virtual bool Accept(JsonVisitor *visitor) const = 0;
protected:
    JsonNode(JsonDocument *);
    virtual ~JsonNode();
    JsonDocument *_document;

    JsonNode *_parent;
    JsonNode *_firstChild;
    JsonNode *_lastChild;

    JsonNode *_prev;
    JsonNode *_next;

private:
    MemPool *_memPool;

    void Unlink(JsonNode *child);

};

//true/false/null
class JsonReserved :public JsonNode
{
    friend JsonDocument;
public:
    enum class Type {
        RESERVED = 0,
        RESERVED_NULL,
        RESERVED_TRUE,
        RESERVED_FALSE
    };

    JsonReserved::Type GetType() const
    {
        return _type;
    }
    char *ParseDeep(char *json) override;
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    JsonReserved(JsonDocument *doc);
    ~JsonReserved();

    JsonReserved::Type _type;
};

class JsonNumber : public JsonNode
{
    friend JsonDocument;
public:
    float GetValue() const
    {
        return _valueFloat;
    }
    char *ParseDeep(char *json) override;
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    JsonNumber(JsonDocument *doc);
    ~JsonNumber();

    int _valueInt;
    float _valueFloat;
};

class JsonString : public JsonNode
{
    friend JsonDocument;
public:
    const std::string GetStr() const
    {
        return _str.GetStr();
    }
    char *ParseDeep(char *json) override;
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    JsonString(JsonDocument *doc);
    ~JsonString();

    StrPair _str;
};

//key-value ??? '\"'
class JsonElement : public JsonNode
{
    friend JsonDocument;
public:
    char *ParseDeep(char *json) override;
    virtual JsonElement *ToElement()
    {
        return this;
    }
    virtual const JsonElement *ToElement() const
    {
        return this;
    }
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    JsonElement(JsonDocument *doc);
    virtual ~JsonElement();
};

class JsonObject : public JsonNode
{
    friend JsonDocument;
public:
    char *ParseDeep(char *json) override; 
    virtual JsonObject *ToObject()
    {
        return this;
    }
    virtual const JsonObject *ToObject() const
    {
        return this;
    }
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    JsonObject(JsonDocument *doc);
    virtual ~JsonObject();

    char *ParseElement(char *json);

};

class JsonArray : public JsonNode
{
    friend JsonDocument;
public:
    char *ParseDeep(char *json) override;
    virtual JsonArray *ToArray()
    {
        return this;
    }
    virtual const JsonArray *ToArray() const
    {
        return this;
    }
    virtual bool Accept(JsonVisitor *visitor) const;

private:
    JsonArray(JsonDocument *doc);
    virtual ~JsonArray();
};

class JsonDocument : public JsonNode
{
public:
    JsonDocument();
    ~JsonDocument();

    char *Identify(char *json, JsonNode **node);
    JsonElement *CreatElement();

    JsonError Parse(const char *json, size_t nBytes = (size_t)(-1));

    inline void SetError(JsonError error, const char *str1, const char *str2);
    virtual bool Accept(JsonVisitor *visitor) const;
private:
    void InitDocument();

private:
    JsonError _errorID;
    const char *_errorStr1;
    const char *_errorStr2;
    char *_charBuffer;

    MemPoolT< sizeof(JsonObject) > _objectPool;
    MemPoolT< sizeof(JsonArray) > _arrayPool;
    MemPoolT< sizeof(JsonElement) > _elementPool;
    MemPoolT< sizeof(JsonNumber) > _numberPool;
    MemPoolT< sizeof(JsonString) > _stringPool;
    MemPoolT< sizeof(JsonReserved) > _reservedPool;
};

class JsonPrinter : public JsonVisitor
{
public:
    JsonPrinter() : _depth(0)
    {}
    virtual ~JsonPrinter() {}

    const std::string &GetString() const
    {
        return _out;
    }
    virtual bool VisitEnter(const JsonObject &node);
    virtual bool VisitExit(const JsonObject &node);
    virtual bool VisitEnter(const JsonArray &node);
    virtual bool VisitExit(const JsonArray &node);
    virtual bool VisitEnter(const JsonElement &node);
    virtual bool Visit(const JsonNumber &node);
    virtual bool Visit(const JsonString &node);
    virtual bool Visit(const JsonReserved &node);
private:
    void PrintSpace(int depth);
    void PrintPrevSymbol(const JsonNode &node);
private:
    int _depth;
    std::string _out;
};
} //tinyjson
#endif //TINYJSON_INCLUDED