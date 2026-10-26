#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include <nix/node.hxx>
#include <nix/serialized_message.hxx>

#include <algorithm>
#include <exception>
#include <chrono>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <typeinfo>

namespace {

constexpr std::uint64_t channelCapacity = 16;

struct NodeObject {
    PyObject_HEAD
    nix::Node* node;
};

struct PublisherHandle {
    std::unique_ptr<nix::Publisher<nix::SerializedMessage, channelCapacity>> publisher;
    PyObject* owner;
    std::string schema;
    std::string encoding;
};

struct PublisherObject {
    PyObject_HEAD
    PublisherHandle* handle;
};

struct SubscriberHandle {
    std::unique_ptr<nix::Subscriber<nix::SerializedMessage, channelCapacity>> subscriber;
    PyObject* owner;
};

struct SubscriberObject {
    PyObject_HEAD
    SubscriberHandle* handle;
};

PyTypeObject nodeType = {PyVarObject_HEAD_INIT(nullptr, 0)};
PyTypeObject publisherType = {PyVarObject_HEAD_INIT(nullptr, 0)};
PyTypeObject subscriberType = {PyVarObject_HEAD_INIT(nullptr, 0)};

PyObject* nodeNew(PyTypeObject* type, PyObject*, PyObject*) {
    auto* self = reinterpret_cast<NodeObject*>(type->tp_alloc(type, 0));
    if (self != nullptr) {
        self->node = nullptr;
    }
    return reinterpret_cast<PyObject*>(self);
}

void setPythonException(const std::exception& error) {
    PyErr_SetString(PyExc_RuntimeError, error.what());
}

int nodeInit(NodeObject* self, PyObject* args, PyObject* keywords) {
    const char* nodeName = nullptr;
    static const char* keywordNames[] = {"name", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "s:Node", const_cast<char**>(keywordNames), &nodeName)) {
        return -1;
    }
    try {
        delete self->node;
        self->node = new nix::Node(nodeName);
        return 0;
    } catch (const std::exception& error) {
        setPythonException(error);
        return -1;
    }
}

void nodeDealloc(NodeObject* self) {
    delete self->node;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

void publisherDealloc(PublisherObject* self) {
    if (self->handle != nullptr) {
        PyObject* owner = self->handle->owner;
        delete self->handle;
        Py_DECREF(owner);
    }
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

void subscriberDealloc(SubscriberObject* self) {
    if (self->handle != nullptr) {
        PyObject* owner = self->handle->owner;
        delete self->handle;
        Py_DECREF(owner);
    }
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

PyObject* nodeCreatePublisher(NodeObject* self, PyObject* args, PyObject* keywords) {
    const char* topic = nullptr;
    const char* schema = nullptr;
    const char* encoding = nullptr;
    static const char* keywordNames[] = {"topic", "schema", "encoding", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "sss:create_publisher",
            const_cast<char**>(keywordNames), &topic, &schema, &encoding)) {
        return nullptr;
    }
    try {
        auto handle = std::make_unique<PublisherHandle>();
        static_cast<void>(nix::makeSerializedMessage(schema, encoding, nullptr, 0U));
        handle->publisher = self->node->createPublisher<nix::SerializedMessage, channelCapacity>(topic);
        handle->owner = reinterpret_cast<PyObject*>(self);
        handle->schema = schema;
        handle->encoding = encoding;
        auto* publisher = reinterpret_cast<PublisherObject*>(publisherType.tp_alloc(&publisherType, 0));
        if (publisher == nullptr) {
            return nullptr;
        }
        publisher->handle = nullptr;
        Py_INCREF(handle->owner);
        publisher->handle = handle.release();
        return reinterpret_cast<PyObject*>(publisher);
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyObject* publisherPublish(PublisherObject* self, PyObject* args) {
    const char* payload = nullptr;
    Py_ssize_t payloadLength = 0;
    if (!PyArg_ParseTuple(args, "y#:publish", &payload, &payloadLength)) {
        return nullptr;
    }
    try {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(payload);
        const auto message = nix::makeSerializedMessage(self->handle->schema, self->handle->encoding,
            bytes, static_cast<std::size_t>(payloadLength));
        if (!self->handle->publisher->publish(message)) {
            Py_RETURN_FALSE;
        }
        Py_RETURN_TRUE;
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyObject* nodeCreateSubscriber(NodeObject* self, PyObject* args, PyObject* keywords) {
    const char* topic = nullptr;
    int waitForPublisher = 1;
    static const char* keywordNames[] = {"topic", "wait_for_publisher", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "s|p:create_subscriber",
            const_cast<char**>(keywordNames), &topic, &waitForPublisher)) {
        return nullptr;
    }
    try {
        if (waitForPublisher != 0) {
            for (;;) {
                const auto endpoints = self->node->discover(topic);
                const bool publisherAvailable = std::any_of(endpoints.begin(), endpoints.end(), [](const nix::EndpointInfo& endpoint) {
                    return endpoint.role == nix::EndpointRole::Publisher
                        && endpoint.typeName == typeid(nix::SerializedMessage).name();
                });
                if (publisherAvailable) {
                    break;
                }
                PyThreadState* threadState = PyEval_SaveThread();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                PyEval_RestoreThread(threadState);
            }
        }
        auto handle = std::make_unique<SubscriberHandle>();
        handle->subscriber = self->node->createSubscriber<nix::SerializedMessage, channelCapacity>(topic);
        handle->owner = reinterpret_cast<PyObject*>(self);
        auto* subscriber = reinterpret_cast<SubscriberObject*>(subscriberType.tp_alloc(&subscriberType, 0));
        if (subscriber == nullptr) {
            return nullptr;
        }
        subscriber->handle = nullptr;
        Py_INCREF(handle->owner);
        subscriber->handle = handle.release();
        return reinterpret_cast<PyObject*>(subscriber);
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyObject* subscriberReceive(SubscriberObject* self, PyObject*) {
    std::optional<nix::SerializedMessage> received;
    std::exception_ptr failure;
    PyThreadState* threadState = PyEval_SaveThread();
    try {
        received = self->handle->subscriber->receiveBlocking();
    } catch (...) {
        failure = std::current_exception();
    }
    PyEval_RestoreThread(threadState);

    if (failure != nullptr) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& error) {
            setPythonException(error);
            return nullptr;
        }
    }
    if (!received.has_value()) {
        Py_RETURN_NONE;
    }

    try {
        const auto decoded = nix::decodeSerializedMessage(*received);
        PyObject* schemaObject = PyUnicode_FromStringAndSize(decoded.schema.data(),
            static_cast<Py_ssize_t>(decoded.schema.size()));
        PyObject* encodingObject = PyUnicode_FromStringAndSize(decoded.encoding.data(),
            static_cast<Py_ssize_t>(decoded.encoding.size()));
        PyObject* payloadObject = PyBytes_FromStringAndSize(
            reinterpret_cast<const char*>(decoded.payload.data()), static_cast<Py_ssize_t>(decoded.payload.size()));
        if (schemaObject == nullptr || encodingObject == nullptr || payloadObject == nullptr) {
            Py_XDECREF(schemaObject);
            Py_XDECREF(encodingObject);
            Py_XDECREF(payloadObject);
            return nullptr;
        }
        PyObject* result = PyTuple_Pack(3, schemaObject, encodingObject, payloadObject);
        Py_XDECREF(schemaObject);
        Py_XDECREF(encodingObject);
        Py_XDECREF(payloadObject);
        return result;
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyObject* encodeMessage(PyObject*, PyObject* args) {
    const char* schema = nullptr;
    const char* encoding = nullptr;
    const char* payload = nullptr;
    Py_ssize_t payloadLength = 0;
    if (!PyArg_ParseTuple(args, "ssy#:encode_message", &schema, &encoding, &payload, &payloadLength)) {
        return nullptr;
    }
    try {
        const auto* payloadBytes = reinterpret_cast<const std::uint8_t*>(payload);
        const auto message = nix::makeSerializedMessage(schema, encoding, payloadBytes,
            static_cast<std::size_t>(payloadLength));
        return PyBytes_FromStringAndSize(reinterpret_cast<const char*>(message.bytes.data()),
            static_cast<Py_ssize_t>(message.bytes.size()));
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyObject* decodeMessage(PyObject*, PyObject* args) {
    const char* frameBytes = nullptr;
    Py_ssize_t frameLength = 0;
    if (!PyArg_ParseTuple(args, "y#:decode_message", &frameBytes, &frameLength)) {
        return nullptr;
    }
    if (frameLength != static_cast<Py_ssize_t>(nix::serializedMessageCapacity)) {
        PyErr_SetString(PyExc_ValueError, "serialized frame has an invalid size");
        return nullptr;
    }
    nix::SerializedMessage message{};
    std::memcpy(message.bytes.data(), frameBytes, message.bytes.size());
    try {
        const auto decoded = nix::decodeSerializedMessage(message);
        PyObject* schemaObject = PyUnicode_FromStringAndSize(decoded.schema.data(),
            static_cast<Py_ssize_t>(decoded.schema.size()));
        PyObject* encodingObject = PyUnicode_FromStringAndSize(decoded.encoding.data(),
            static_cast<Py_ssize_t>(decoded.encoding.size()));
        PyObject* payloadObject = PyBytes_FromStringAndSize(
            reinterpret_cast<const char*>(decoded.payload.data()), static_cast<Py_ssize_t>(decoded.payload.size()));
        if (schemaObject == nullptr || encodingObject == nullptr || payloadObject == nullptr) {
            Py_XDECREF(schemaObject);
            Py_XDECREF(encodingObject);
            Py_XDECREF(payloadObject);
            return nullptr;
        }
        PyObject* result = PyTuple_Pack(3, schemaObject, encodingObject, payloadObject);
        Py_DECREF(schemaObject);
        Py_DECREF(encodingObject);
        Py_DECREF(payloadObject);
        return result;
    } catch (const std::exception& error) {
        PyErr_SetString(PyExc_ValueError, error.what());
        return nullptr;
    }
}

PyObject* nodeDiscover(NodeObject* self, PyObject* args, PyObject* keywords) {
    const char* topic = "";
    static const char* keywordNames[] = {"topic", nullptr};
    if (!PyArg_ParseTupleAndKeywords(args, keywords, "|s:discover",
            const_cast<char**>(keywordNames), &topic)) {
        return nullptr;
    }
    try {
        const auto endpoints = self->node->discover(topic);
        PyObject* result = PyList_New(static_cast<Py_ssize_t>(endpoints.size()));
        if (result == nullptr) {
            return nullptr;
        }
        for (std::size_t index = 0; index < endpoints.size(); ++index) {
            const auto& endpoint = endpoints[index];
            PyObject* item = Py_BuildValue("{s:s,s:s,s:s,s:s,s:K}",
                "node", endpoint.nodeName.c_str(),
                "topic", endpoint.topicName.c_str(),
                "type", endpoint.typeName.c_str(),
                "role", endpoint.role == nix::EndpointRole::Publisher ? "publisher" : "subscriber",
                "capacity", static_cast<unsigned long long>(endpoint.capacity));
            if (item == nullptr) {
                Py_DECREF(result);
                return nullptr;
            }
            PyList_SET_ITEM(result, static_cast<Py_ssize_t>(index), item);
        }
        return result;
    } catch (const std::exception& error) {
        setPythonException(error);
        return nullptr;
    }
}

PyMethodDef nodeMethods[] = {
    {"create_publisher", reinterpret_cast<PyCFunction>(nodeCreatePublisher), METH_VARARGS | METH_KEYWORDS,
        "Create a publisher for serialized byte payloads."},
    {"create_subscriber", reinterpret_cast<PyCFunction>(nodeCreateSubscriber), METH_VARARGS | METH_KEYWORDS,
        "Create a blocking subscriber for serialized messages."},
    {"discover", reinterpret_cast<PyCFunction>(nodeDiscover), METH_VARARGS | METH_KEYWORDS,
        "List registered endpoints, optionally filtered by topic."},
    {nullptr, nullptr, 0, nullptr},
};

PyMethodDef publisherMethods[] = {
    {"publish", reinterpret_cast<PyCFunction>(publisherPublish), METH_VARARGS,
        "Publish bytes using this publisher's configured schema and encoding."},
    {nullptr, nullptr, 0, nullptr},
};

PyMethodDef subscriberMethods[] = {
    {"receive", reinterpret_cast<PyCFunction>(subscriberReceive), METH_NOARGS,
        "Block until a message arrives and return (schema, encoding, payload)."},
    {nullptr, nullptr, 0, nullptr},
};

PyMethodDef moduleMethods[] = {
    {"encode_message", encodeMessage, METH_VARARGS,
        "Encode schema, encoding, and payload bytes into the canonical Nix frame."},
    {"decode_message", decodeMessage, METH_VARARGS,
        "Validate and decode a canonical Nix frame."},
    {nullptr, nullptr, 0, nullptr},
};

PyModuleDef moduleDefinition = {
    PyModuleDef_HEAD_INIT,
    "_nixipc",
    "Native Nix IPC node and serialized message bindings.",
    -1,
    moduleMethods,
};

} // namespace

PyMODINIT_FUNC PyInit__nixipc() {
    nodeType.tp_name = "nixipc.Node";
    nodeType.tp_basicsize = sizeof(NodeObject);
    nodeType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    nodeType.tp_new = nodeNew;
    nodeType.tp_init = reinterpret_cast<initproc>(nodeInit);
    nodeType.tp_dealloc = reinterpret_cast<destructor>(nodeDealloc);
    nodeType.tp_methods = nodeMethods;

    publisherType.tp_name = "nixipc.Publisher";
    publisherType.tp_basicsize = sizeof(PublisherObject);
    publisherType.tp_flags = Py_TPFLAGS_DEFAULT;
    publisherType.tp_new = PyType_GenericNew;
    publisherType.tp_dealloc = reinterpret_cast<destructor>(publisherDealloc);
    publisherType.tp_methods = publisherMethods;

    subscriberType.tp_name = "nixipc.Subscriber";
    subscriberType.tp_basicsize = sizeof(SubscriberObject);
    subscriberType.tp_flags = Py_TPFLAGS_DEFAULT;
    subscriberType.tp_new = PyType_GenericNew;
    subscriberType.tp_dealloc = reinterpret_cast<destructor>(subscriberDealloc);
    subscriberType.tp_methods = subscriberMethods;

    if (PyType_Ready(&nodeType) < 0 || PyType_Ready(&publisherType) < 0
        || PyType_Ready(&subscriberType) < 0) {
        return nullptr;
    }

    PyObject* module = PyModule_Create(&moduleDefinition);
    if (module == nullptr) {
        return nullptr;
    }
    Py_INCREF(&nodeType);
    Py_INCREF(&publisherType);
    Py_INCREF(&subscriberType);
    if (PyModule_AddObject(module, "Node", reinterpret_cast<PyObject*>(&nodeType)) < 0
        || PyModule_AddObject(module, "Publisher", reinterpret_cast<PyObject*>(&publisherType)) < 0
        || PyModule_AddObject(module, "Subscriber", reinterpret_cast<PyObject*>(&subscriberType)) < 0) {
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}