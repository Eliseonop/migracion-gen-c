#pragma once

// JSON del núcleo (gc::Json, ordenado) y los QVariant que ve QML, ida y vuelta por QJsonDocument.

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariant>
#include <QVariantMap>

#include "gc/core/json.hpp"

namespace gc::studio {

inline Json to_json(const QVariantMap& map) {
    return Json::parse(QJsonDocument(QJsonObject::fromVariantMap(map)).toJson().toStdString());
}

inline QVariantMap to_variant(const Json& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(json.dump())).object().toVariantMap();
}

inline QVariant to_qvariant(const Json& json) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(Json::array({json}).dump())).array().at(0).toVariant();
}

} // namespace gc::studio
