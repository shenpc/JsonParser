
#include <sstream>
#include "tinyJson.h"

namespace tinyjson
{
JsonNode::JsonNode(JsonDocument *doc) :
    _document(doc),
    _parent(nullptr),
    _firstChild(nullptr),
    _lastChild(nullptr),
    _next(nullptr),
    _prev(nullptr)
{
}

JsonNode::~JsonNode()
{
    DeleteChildren();
    if (_parent != nullptr) {
        _parent->Unlink(this);
    }
}

void JsonNode::DeleteChildren()
{
    while (_firstChild != nullptr) {
        JsonNode *node = _firstChild;
        Unlink(node);

        DeleteNode(node);
    }
    _firstChild = _lastChild = nullptr;
}


void JsonNode::Unlink(JsonNode *child)
{
    TJASSERT(child->_parent == this);
    if (child == _firstChild) {
        _firstChild = _firstChild->_next;
    }
    if (child == _lastChild) {
        _lastChild = _lastChild->_prev;
    }

    if (child->_prev != nullptr) {
        child->_prev->_next = child->_next;
    }
    if (child->_next != nullptr) {
        child->_next->_prev = child->_prev;
    }
    child->_parent = nullptr;
}

JsonNode *JsonNode::InsertEndChild(JsonNode *node)
{
    if (_lastChild) {
        TJASSERT(_firstChild);
        TJASSERT(_lastChild->_next == 0);
        _lastChild->_next = node;
        node->_prev = _lastChild;
        _lastChild = node;

        node->_next = 0;
    }
    else {
        TJASSERT(_firstChild == 0);
        _firstChild = _lastChild = node;

        node->_prev = 0;
        node->_next = 0;
    }
    node->_parent = this;
#ifdef DEBUG
    node->_memPool->SetTracked();
#endif // DEBUG

    return node;
}

char *JsonNode::ParseDeep(char *json)
{
    while (json != nullptr && *json) {
        JsonNode *node = nullptr;

        json = _document->Identify(json, &node);
        if (json == nullptr || node == nullptr) {
            break;
        }

        json = JsonUtil::SkipWhiteSpace(node->ParseDeep(json));
        if (json == nullptr) {
#ifdef DEBUG
            node->GetMemPool()->SetTracked();
#endif
            DeleteNode(node);
            node = nullptr;
            _document->SetError(JsonError::JSON_ERROR_PARSING, 0, 0);
            break;
        }

        if (node != nullptr) {
            this->InsertEndChild(node);
        }
    }
    return json;
}

/********************************************************************************************/
JsonReserved::JsonReserved(JsonDocument *doc) : JsonNode(doc),
    _type(JsonReserved::Type::RESERVED)
{}

JsonReserved::~JsonReserved()
{}

char *JsonReserved::ParseDeep(char *json)
{
    if (!strncmp(json, "null", 4)) {
        _type = JsonReserved::Type::RESERVED_NULL;
        json += 4;
    } else if (!strncmp(json, "true", 4)) {
        _type = JsonReserved::Type::RESERVED_TRUE;
        json += 4;
    } else if (!strncmp(json, "false", 5)) {
        _type = JsonReserved::Type::RESERVED_FALSE;
        json += 5;
    } else {
        _document->SetError(JsonError::JSON_ERROR_PARSING_RESERVED, 0, 0);
        json = nullptr;
    }

    return json;
}

bool JsonReserved::Accept(JsonVisitor *visitor) const
{
    return visitor->Visit(*this);
}

JsonNumber::JsonNumber(JsonDocument *doc) : JsonNode(doc),
    _valueFloat(0.0f),
    _valueInt(0)
{
}

JsonNumber::~JsonNumber()
{
}

char *JsonNumber::ParseDeep(char *json)
{
    char *endptr;
    float n;
    json = JsonUtil::SkipWhiteSpace(json);
    if (json == nullptr) {
        return nullptr;
    }

#if __STDC_VERSION__ >= 199901L
    n = strtof(num, &endptr);
#else
    n = (float)strtod(json, &endptr);
#endif

    if (endptr != json) {
        _valueFloat = n;
        _valueInt = (int)n;
        return endptr;
    }
    _document->SetError(JsonError::JSON_ERROR_OBJECT_MISMATCH, 0, 0);

    return nullptr;
}

bool JsonNumber::Accept(JsonVisitor *visitor) const
{
    return visitor->Visit(*this);
}

JsonString::JsonString(JsonDocument *doc) : JsonNode(doc)
{
}

JsonString::~JsonString()
{
}

char *JsonString::ParseDeep(char *json)
{
    char *ptr = json;
    int len = 0;
    while (*ptr != '\"' && *ptr && ++len) {
        if (*ptr++ == '\\') {
            ptr++;
        }
    }
    if (ptr == nullptr || !*ptr) {
        _document->SetError(JsonError::JSON_ERROR_PARSING_STRING, 0, 0);
        return nullptr;
    }
    *ptr = 0;
    _str.Set(json, ptr);
    json = ptr + 1;
    return json;
}

bool JsonString::Accept(JsonVisitor *visitor) const
{
    return visitor->Visit(*this);
}

/********************************************************************************************/

JsonElement::JsonElement(JsonDocument *doc) : JsonNode(doc)
{
}

JsonElement::~JsonElement()
{
}

char *JsonElement::ParseDeep(char *json)
{
    do{
        if (*json != '\"') {
            break;
        }
        //key
        json = JsonUtil::SkipWhiteSpace(JsonNode::ParseDeep(json));
        if (json == nullptr || *json != ':') {
            break;
        }
        ++json;
        //value
        json = JsonUtil::SkipWhiteSpace(JsonNode::ParseDeep(json));
        if (json == nullptr) {
            break;
        }
        return json;
    } while (false);

    _document->SetError(JsonError::JSON_ERROR_PARSING_ELEMENT, 0, 0);
    return nullptr;
}

bool JsonElement::Accept(JsonVisitor *visitor) const
{
    if (visitor->VisitEnter(*this)) {
        for (const JsonNode *node = FirstChild(); node; node = node->NextSibling()) {
            if (!node->Accept(visitor)) {
                break;
            }
        }
    }
    return visitor->VisitExit(*this);
}

/********************************************************************************************/

JsonObject::JsonObject(JsonDocument *doc) : JsonNode(doc)
{
}


JsonObject::~JsonObject()
{
}

char *JsonObject::ParseDeep(char *json)
{
    json = JsonUtil::SkipWhiteSpace(json);
    if (json == nullptr || !*json) {
        _document->SetError(JsonError::JSON_ERROR_OBJECT_MISMATCH, 0, 0);
        return nullptr;
    }

    if (*json == '}') {
        ++json;
        return json;
    }

    json = ParseElement(json);

    while (*json == ',') {
        ++json;
        json = ParseElement(json);
        if (json == nullptr) {
            _document->SetError(JsonError::JSON_ERROR_OBJECT_MISMATCH, 0, 0);
            return nullptr;
        }
    }

    if (*json == '}') {
        ++json;
        return json;
    }

    _document->SetError(JsonError::JSON_ERROR_OBJECT_MISMATCH, 0, 0);
    return nullptr;
}

char *JsonObject::ParseElement(char *json) {
    JsonElement *node = _document->CreatElement();
    json = JsonUtil::SkipWhiteSpace(node->ParseDeep(json));
    if (json == nullptr) {
#ifdef DEBUG
        node->GetMemPool()->SetTracked();
#endif
        DeleteNode(node);
        node = nullptr;
        _document->SetError(JsonError::JSON_ERROR_PARSING_ELEMENT, 0, 0);
        return nullptr;
    }
    InsertEndChild(node);
    return json;
}

bool JsonObject::Accept(JsonVisitor *visitor) const
{
    if (visitor->VisitEnter(*this)) {
        for (const JsonNode *node = FirstChild(); node; node = node->NextSibling()) {
            if (!node->Accept(visitor)) {
                break;
            }
        }
    }
    return visitor->VisitExit(*this);
}
/********************************************************************************************/

JsonArray::JsonArray(JsonDocument *doc) : JsonNode(doc)
{
}

JsonArray::~JsonArray()
{
}

char *JsonArray::ParseDeep(char *json)
{
    json = JsonUtil::SkipWhiteSpace(json);
    if (json == nullptr || !*json) {
        _document->SetError(JsonError::JSON_ERROR_ARRAY_MISMATCH, 0, 0);
        return nullptr;
    }

    if (*json == ']') {
        ++json;
        return json;
    }
    
    json = JsonUtil::SkipWhiteSpace(JsonNode::ParseDeep(json));
    if (json == nullptr) {
        return nullptr;
    }
    while (*json == ',') {
        ++json;
        json = JsonUtil::SkipWhiteSpace(JsonNode::ParseDeep(json));
        if (json == nullptr || !*json) {
            _document->SetError(JsonError::JSON_ERROR_ARRAY_MISMATCH, 0, 0);
            return nullptr;
        }
    }
    if (*json == ']') {
        ++json;
        return json;
    }

    _document->SetError(JsonError::JSON_ERROR_ARRAY_MISMATCH, 0, 0);
    return nullptr;
}

bool JsonArray::Accept(JsonVisitor *visitor) const
{
    if (visitor->VisitEnter(*this)) {
        for (const JsonNode *node = FirstChild(); node; node = node->NextSibling()) {
            if (!node->Accept(visitor)) {
                break;
            }
        }
    }
    return visitor->VisitExit(*this);
}
/********************************************************************************************/
JsonDocument::JsonDocument() :
    JsonNode(nullptr),
    _errorID(JsonError::JSON_NO_ERROR),
    _errorStr1(nullptr),
    _errorStr2(nullptr),
    _charBuffer(nullptr)
{
    _document = this;
}

JsonDocument::~JsonDocument()
{
    DeleteChildren();
    delete[] _charBuffer;
    _charBuffer = nullptr;

#ifdef DEBUG
    if (_errorID == JsonError::JSON_NO_ERROR ) {
        TJASSERT(_objectPool.CurrentAllocs() == _objectPool.Untracked());
        TJASSERT(_arrayPool.CurrentAllocs() == _arrayPool.Untracked());
        TJASSERT(_elementPool.CurrentAllocs() == _elementPool.Untracked());
        TJASSERT(_numberPool.CurrentAllocs() == _numberPool.Untracked());
        TJASSERT(_stringPool.CurrentAllocs() == _stringPool.Untracked());
        TJASSERT(_reservedPool.CurrentAllocs() == _reservedPool.Untracked());
    }
#endif
}

void JsonDocument::InitDocument()
{
    _errorID = JsonError::JSON_NO_ERROR;
    _errorStr1 = nullptr;
    _errorStr2 = nullptr;

    delete[] _charBuffer;
    _charBuffer = nullptr;
}

void JsonDocument::SetError(JsonError error, const char *str1, const char *str2)
{
    if (_errorID == JsonError::JSON_NO_ERROR) {
        _errorID = error;
        _errorStr1 = str1;
        _errorStr2 = str2;
    }
}

char *JsonDocument::Identify(char *json, JsonNode **node)
{
    JsonNode *returnNode = nullptr;
    char *start = json;
    json = JsonUtil::SkipWhiteSpace(json);
    if (json == nullptr || !*json) {
        return json;
    }

    switch (*json)
    {
    case 'n':
    case 't':
    case 'f':
        returnNode = new (_reservedPool.Alloc()) JsonReserved(this);
        returnNode->_memPool = &_reservedPool;
        break;
    case '\"':
        returnNode = new (_stringPool.Alloc()) JsonString(this);
        returnNode->_memPool = &_stringPool;
        ++json;
        break;
    case '{':
        returnNode = new (_objectPool.Alloc()) JsonObject(this);
        returnNode->_memPool = &_objectPool;
        ++json;
        break;
    case '[':
        returnNode = new (_arrayPool.Alloc()) JsonArray(this);
        returnNode->_memPool = &_arrayPool;
        ++json;
        break;
    case '-':
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        returnNode = new (_numberPool.Alloc()) JsonNumber(this);
        returnNode->_memPool = &_numberPool;
        break;
    default:
        break;
    }

    *node = returnNode;
    return json;
}

JsonElement *JsonDocument::CreatElement()
{
    JsonElement *node = new (_elementPool.Alloc()) JsonElement(this);
    node->_memPool = &_elementPool;
    return node;
}

JsonError JsonDocument::Parse(const char *json, size_t len)
{
    DeleteChildren();
    InitDocument();

    if (json == nullptr || !*json) {
        SetError(JsonError::JSON_ERROR_EMPTY_DOCUMENT, 0, 0);
        return _errorID;
    }
    if (len == (size_t)(-1)) {
        len = strlen(json);
    }
    _charBuffer = new char[len + 1];
    memcpy(_charBuffer, json, len);
    _charBuffer[len] = 0;

    json = JsonUtil::SkipWhiteSpace(json);
    if (json == nullptr || !*json) {
        SetError(JsonError::JSON_ERROR_EMPTY_DOCUMENT, 0, 0);
        return _errorID;
    }

    ParseDeep(_charBuffer);
    return _errorID;
}

bool JsonDocument::Accept(JsonVisitor *visitor) const
{
    for (const JsonNode *node = FirstChild(); node; node = node->NextSibling()) {
        if (!node->Accept(visitor)) {
            break;
        }
    }
    return true;
}

bool JsonPrinter::VisitEnter(const JsonObject &node)
{
    PrintPrevSymbol(node);
    _out.append("{\n");
    ++_depth;
    return true;
}

bool JsonPrinter::VisitExit(const JsonObject &node)
{
    _out.append("\n");
    --_depth;
    PrintSpace(_depth);
    _out.append("}");
    return true;
}

bool JsonPrinter::VisitEnter(const JsonArray &node)
{
    PrintPrevSymbol(node);
    _out.append("[\n");
    ++_depth;
    return true;
}

bool JsonPrinter::VisitExit(const JsonArray &node)
{
    _out.append("\n");
    --_depth;
    PrintSpace(_depth);
    _out.append("]");
    return true;
}

bool JsonPrinter::VisitEnter(const JsonElement &node)
{
    PrintPrevSymbol(node);
    return true;
}

bool JsonPrinter::Visit(const JsonNumber &node)
{
    PrintPrevSymbol(node);
    std::ostringstream ss;
    ss << node.GetValue();
    _out += ss.str();
    return true;
}

bool JsonPrinter::Visit(const JsonString &node)
{
    PrintPrevSymbol(node);

    std::ostringstream ss;
    ss << '\"'<<node.GetStr() << '\"';
    _out += ss.str();
    return true;
}

bool JsonPrinter::Visit(const JsonReserved &node)
{
    PrintPrevSymbol(node);
    switch (node.GetType())
    {
    case JsonReserved::Type::RESERVED_NULL:
        _out.append("null");
        break;
    case JsonReserved::Type::RESERVED_TRUE:
        _out.append("true");
        break;
    case JsonReserved::Type::RESERVED_FALSE:
        _out.append("flase");
        break;
    default:
        break;
    }
    return true;
}

void JsonPrinter::PrintSpace(int depth)
{
    for (int i = 0; i < depth; ++i) {
        _out.append("    ");
    }
}

void JsonPrinter::PrintPrevSymbol(const JsonNode &node)
{
    const JsonNode *parent = node.Parent();
    if (parent != nullptr && node.PreviousSibling() != nullptr) {
        if (parent->ToElement() != nullptr) {
            _out.append(" : ");
            return;
        }
        else {
            _out.append(",\n");
        }
    }
    if (node.ToElement() == nullptr) {
        PrintSpace(_depth);
    }
}

}//tinyjson