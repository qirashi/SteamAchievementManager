/* Copyright (c) 2024 Rick (rick 'at' gibbed 'dot' us)
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would
 *    be appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not
 *    be misrepresented as being the original software.
 *
 * 3. This notice may not be removed or altered from any source
 *    distribution.
 *
 * Altered source version: Qt 6 port of Steam Achievement Manager.
 */

#include "KeyValue.h"

#include <QFile>

#include <cstring>
#include <stdexcept>

namespace sam
{
namespace
{
const KeyValue &invalidNode()
{
    static const KeyValue node;
    return node;
}

class Reader
{
public:
    explicit Reader(const QByteArray &data) : m_data(data.constData()), m_size(data.size()) {}

    bool atEnd() const { return m_position >= m_size; }
    qsizetype position() const { return m_position; }
    qsizetype size() const { return m_size; }

    uint8_t readU8()
    {
        require(1);
        return static_cast<uint8_t>(m_data[m_position++]);
    }

    template <typename T> T read()
    {
        require(sizeof(T));
        T value;
        std::memcpy(&value, m_data + m_position, sizeof(T));
        m_position += sizeof(T);
        return value;
    }

    QString readString()
    {
        const void *end = std::memchr(m_data + m_position, 0, static_cast<size_t>(m_size - m_position));
        if (end == nullptr)
        {
            throw std::runtime_error("unterminated string");
        }
        const auto length = static_cast<const char *>(end) - (m_data + m_position);
        QString value = QString::fromUtf8(m_data + m_position, length);
        m_position += length + 1;
        return value;
    }

private:
    void require(qsizetype count) const
    {
        if (m_position + count > m_size)
        {
            throw std::runtime_error("unexpected end of data");
        }
    }

    const char *m_data;
    qsizetype m_size;
    qsizetype m_position = 0;
};

void readChildren(Reader &reader, KeyValue &parent, int depth)
{
    if (depth > 64)
    {
        throw std::runtime_error("nesting too deep");
    }

    while (true)
    {
        const auto type = static_cast<KeyValue::Type>(reader.readU8());
        if (type == KeyValue::Type::End)
        {
            break;
        }

        KeyValue current;
        current.type = type;
        current.name = reader.readString();

        switch (type)
        {
            case KeyValue::Type::None: readChildren(reader, current, depth + 1); break;
            case KeyValue::Type::String:
                current.valid = true;
                current.value = reader.readString();
                break;
            case KeyValue::Type::Int32:
                current.valid = true;
                current.value = reader.read<int32_t>();
                break;
            case KeyValue::Type::UInt64:
                current.valid = true;
                current.value = static_cast<qulonglong>(reader.read<uint64_t>());
                break;
            case KeyValue::Type::Float32:
                current.valid = true;
                current.value = reader.read<float>();
                break;
            case KeyValue::Type::Color:
            case KeyValue::Type::Pointer:
                current.valid = true;
                current.value = reader.read<uint32_t>();
                break;
            default: throw std::runtime_error("unsupported value type");
        }

        if (reader.atEnd())
        {
            throw std::runtime_error("unexpected end of data");
        }

        parent.children.append(std::move(current));
    }

    parent.valid = true;
}
} // namespace

const KeyValue &KeyValue::operator[](QStringView key) const
{
    for (const auto &child : children)
    {
        if (child.name.compare(key, Qt::CaseInsensitive) == 0)
        {
            return child;
        }
    }
    return invalidNode();
}

QString KeyValue::asString(const QString &defaultValue) const
{
    if (!valid || !value.isValid())
    {
        return defaultValue;
    }
    return value.toString();
}

int KeyValue::asInteger(int defaultValue) const
{
    if (!valid)
    {
        return defaultValue;
    }

    switch (type)
    {
        case Type::String:
        case Type::WideString: {
            bool ok = false;
            const int result = value.toString().trimmed().toInt(&ok);
            return ok ? result : defaultValue;
        }
        case Type::Int32: return value.toInt();
        case Type::Float32: return static_cast<int>(value.toFloat());
        case Type::UInt64: return static_cast<int>(value.toULongLong() & 0xFFFFFFFFu);
        default: return defaultValue;
    }
}

float KeyValue::asFloat(float defaultValue) const
{
    if (!valid)
    {
        return defaultValue;
    }

    switch (type)
    {
        case Type::String:
        case Type::WideString: {
            bool ok = false;
            const float result = value.toString().trimmed().toFloat(&ok);
            return ok ? result : defaultValue;
        }
        case Type::Int32: return static_cast<float>(value.toInt());
        case Type::Float32: return value.toFloat();
        case Type::UInt64: return static_cast<float>(value.toULongLong() & 0xFFFFFFFFu);
        default: return defaultValue;
    }
}

bool KeyValue::asBoolean(bool defaultValue) const
{
    if (!valid)
    {
        return defaultValue;
    }

    switch (type)
    {
        case Type::String:
        case Type::WideString: {
            bool ok = false;
            const int result = value.toString().trimmed().toInt(&ok);
            return ok ? result != 0 : defaultValue;
        }
        case Type::Int32: return value.toInt() != 0;
        case Type::Float32: return static_cast<int>(value.toFloat()) != 0;
        case Type::UInt64: return value.toULongLong() != 0;
        default: return defaultValue;
    }
}

std::optional<KeyValue> KeyValue::loadBinary(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }
    return parseBinary(file.readAll());
}

std::optional<KeyValue> KeyValue::parseBinary(const QByteArray &data)
{
    try
    {
        Reader reader(data);
        KeyValue root;
        readChildren(reader, root, 0);
        return root;
    }
    catch (const std::exception &)
    {
        return std::nullopt;
    }
}
} // namespace sam
