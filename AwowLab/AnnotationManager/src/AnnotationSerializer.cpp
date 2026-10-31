#include "AnnotationSerializer.h"
#include "Core/ErrorLogger.h"
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/prettywriter.h>

namespace AnnotationSerializer {

namespace {

// Annotation files are shared between players, so any field can hold the
// wrong type, and rapidjson asserts on a Get of the wrong type. A field that
// is present but wrongly typed makes the reader skip that entry.
bool readFloats(const rapidjson::Value& array, float* out, rapidjson::SizeType count) {
    for (rapidjson::SizeType i = 0; i < array.Size() && i < count; ++i) {
        if (!array[i].IsNumber()) return false;
        out[i] = array[i].GetFloat();
    }
    return true;
}

} // namespace

std::string toJson(const std::vector<Annotation>& annotations, const AnnotationKey& key) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", 1, alloc);

    doc.AddMember("encounterId", key.encounterId, alloc);
    rapidjson::Value encounterName;
    encounterName.SetString(key.encounterName.c_str(),
                            static_cast<rapidjson::SizeType>(key.encounterName.length()), alloc);
    doc.AddMember("encounterName", encounterName, alloc);

    rapidjson::Value annotationsArray(rapidjson::kArrayType);

    for (const auto& annotation : annotations) {
        if (annotation.deleted) continue;

        rapidjson::Value obj(rapidjson::kObjectType);

        obj.AddMember("id", annotation.id, alloc);

        rapidjson::Value shapeStr;
        shapeStr.SetString(annotationShapeToString(annotation.shape), alloc);
        obj.AddMember("shape", shapeStr, alloc);

        rapidjson::Value colorArray(rapidjson::kArrayType);
        for (int i = 0; i < 4; ++i) {
            colorArray.PushBack(annotation.color[i], alloc);
        }
        obj.AddMember("color", colorArray, alloc);

        rapidjson::Value dimsArray(rapidjson::kArrayType);
        for (int i = 0; i < 4; ++i) {
            dimsArray.PushBack(annotation.dimensions[i], alloc);
        }
        obj.AddMember("dimensions", dimsArray, alloc);

        // Point count (for Star shape)
        obj.AddMember("pointCount", annotation.pointCount, alloc);

        rapidjson::Value anchorStr;
        anchorStr.SetString(annotationAnchorToString(annotation.anchorMode), alloc);
        obj.AddMember("anchorMode", anchorStr, alloc);

        obj.AddMember("worldX", annotation.worldX, alloc);
        obj.AddMember("worldY", annotation.worldY, alloc);

        rapidjson::Value guidStr;
        guidStr.SetString(annotation.actorGuid.c_str(),
                          static_cast<rapidjson::SizeType>(annotation.actorGuid.length()), alloc);
        obj.AddMember("actorGuid", guidStr, alloc);

        rapidjson::Value durationStr;
        durationStr.SetString(annotationDurationToString(annotation.durationType), alloc);
        obj.AddMember("durationType", durationStr, alloc);

        obj.AddMember("creationTimestamp", annotation.creationTimestamp_ms, alloc);
        obj.AddMember("durationMs", annotation.durationMs, alloc);

        obj.AddMember("visible", annotation.visible, alloc);

        rapidjson::Value visualStyleStr;
        visualStyleStr.SetString(annotationVisualStyleToString(annotation.visualStyle), alloc);
        obj.AddMember("visualStyle", visualStyleStr, alloc);
        obj.AddMember("stripeSpacing", annotation.stripeSpacing, alloc);
        obj.AddMember("rippleSpeed", annotation.rippleSpeed, alloc);
        obj.AddMember("rippleCount", annotation.rippleCount, alloc);

        rapidjson::Value motionTypeStr;
        motionTypeStr.SetString(annotationMotionTypeToString(annotation.motion.type), alloc);
        obj.AddMember("motionType", motionTypeStr, alloc);
        obj.AddMember("rotationSpeed_degPerSec", annotation.motion.rotationSpeed_degPerSec, alloc);
        obj.AddMember("motionEndOffsetX", annotation.motion.endOffsetX, alloc);
        obj.AddMember("motionEndOffsetY", annotation.motion.endOffsetY, alloc);
        obj.AddMember("motionTravelTime_ms", annotation.motion.travelTime_ms, alloc);
        rapidjson::Value endBehaviorStr;
        endBehaviorStr.SetString(projectileEndBehaviorToString(annotation.motion.endBehavior), alloc);
        obj.AddMember("projectileEndBehavior", endBehaviorStr, alloc);

        obj.AddMember("dropsPerSecond", annotation.motion.dropsPerSecond, alloc);
        obj.AddMember("dropRadius", annotation.motion.dropRadius, alloc);
        obj.AddMember("dropLifetime_ms", annotation.motion.dropLifetime_ms, alloc);

        if (annotation.worldMarkerType != WorldMarkerType::None) {
            rapidjson::Value worldMarkerStr;
            worldMarkerStr.SetString(worldMarkerTypeToString(annotation.worldMarkerType), alloc);
            obj.AddMember("worldMarkerType", worldMarkerStr, alloc);
            obj.AddMember("markerHeight", annotation.markerHeight, alloc);
        }

        annotationsArray.PushBack(obj, alloc);
    }

    doc.AddMember("annotations", annotationsArray, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

bool fromJson(const std::string& json, std::vector<Annotation>& annotations, uint64_t& maxIdFound) {
    rapidjson::Document doc;
    doc.Parse(json.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to parse annotation JSON");
        return false;
    }

    if (!doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != 1) {
        AWOW_LOG_ERROR("ANNOTATION", "Unsupported annotation file version (expected v1)");
        return false;
    }

    if (!doc.HasMember("annotations") || !doc["annotations"].IsArray()) {
        return false;
    }

    annotations.clear();
    maxIdFound = 0;

    const auto& annotationsArray = doc["annotations"].GetArray();
    for (const auto& obj : annotationsArray) {
        if (!obj.IsObject()) continue;

        // Present with the wrong type: the entry is skipped
        auto wrongType = [&obj](const char* key, bool (rapidjson::Value::*isType)() const) {
            return obj.HasMember(key) && !(obj[key].*isType)();
        };
        if (wrongType("id", &rapidjson::Value::IsUint64) ||
            wrongType("shape", &rapidjson::Value::IsString) ||
            wrongType("color", &rapidjson::Value::IsArray) ||
            wrongType("dimensions", &rapidjson::Value::IsArray) ||
            wrongType("anchorMode", &rapidjson::Value::IsString) ||
            wrongType("worldX", &rapidjson::Value::IsNumber) ||
            wrongType("worldY", &rapidjson::Value::IsNumber) ||
            wrongType("actorGuid", &rapidjson::Value::IsString) ||
            wrongType("durationType", &rapidjson::Value::IsString) ||
            wrongType("creationTimestamp", &rapidjson::Value::IsInt) ||
            wrongType("durationMs", &rapidjson::Value::IsInt) ||
            wrongType("visible", &rapidjson::Value::IsBool)) {
            continue;
        }

        Annotation annotation;

        if (obj.HasMember("color") &&
            !readFloats(obj["color"], annotation.color.data(), 4)) {
            continue;
        }
        if (obj.HasMember("dimensions") &&
            !readFloats(obj["dimensions"], annotation.dimensions.data(), 4)) {
            continue;
        }

        if (obj.HasMember("id")) {
            annotation.id = obj["id"].GetUint64();
            maxIdFound = std::max(maxIdFound, annotation.id);
        }

        if (obj.HasMember("shape")) {
            annotation.shape = stringToAnnotationShape(obj["shape"].GetString());
        }

        // Point count (for Star shape)
        if (obj.HasMember("pointCount") && obj["pointCount"].IsUint()) {
            annotation.pointCount = static_cast<uint8_t>(obj["pointCount"].GetUint());
        }

        if (obj.HasMember("anchorMode")) {
            annotation.anchorMode = stringToAnnotationAnchor(obj["anchorMode"].GetString());
        }

        if (obj.HasMember("worldX")) {
            annotation.worldX = obj["worldX"].GetFloat();
            annotation.currentX = annotation.worldX;
        }

        if (obj.HasMember("worldY")) {
            annotation.worldY = obj["worldY"].GetFloat();
            annotation.currentY = annotation.worldY;
        }

        if (obj.HasMember("actorGuid")) {
            annotation.actorGuid = obj["actorGuid"].GetString();
        }

        if (obj.HasMember("durationType")) {
            annotation.durationType = stringToAnnotationDuration(obj["durationType"].GetString());
        }

        if (obj.HasMember("creationTimestamp")) {
            annotation.creationTimestamp_ms = obj["creationTimestamp"].GetInt();
        }

        if (obj.HasMember("durationMs")) {
            annotation.durationMs = obj["durationMs"].GetInt();
        }

        if (obj.HasMember("visible")) {
            annotation.visible = obj["visible"].GetBool();
        }

        // Fields from here on are optional; older files leave them at their defaults
        if (obj.HasMember("visualStyle") && obj["visualStyle"].IsString()) {
            annotation.visualStyle = stringToAnnotationVisualStyle(obj["visualStyle"].GetString());
        }
        if (obj.HasMember("stripeSpacing") && obj["stripeSpacing"].IsNumber()) {
            annotation.stripeSpacing = obj["stripeSpacing"].GetFloat();
        }
        if (obj.HasMember("rippleSpeed") && obj["rippleSpeed"].IsNumber()) {
            annotation.rippleSpeed = obj["rippleSpeed"].GetFloat();
        }
        if (obj.HasMember("rippleCount") && obj["rippleCount"].IsNumber()) {
            annotation.rippleCount = obj["rippleCount"].GetFloat();
        }

        if (obj.HasMember("motionType") && obj["motionType"].IsString()) {
            annotation.motion.type = stringToAnnotationMotionType(obj["motionType"].GetString());
        }
        if (obj.HasMember("rotationSpeed_degPerSec") && obj["rotationSpeed_degPerSec"].IsNumber()) {
            annotation.motion.rotationSpeed_degPerSec = obj["rotationSpeed_degPerSec"].GetFloat();
        }
        if (obj.HasMember("motionEndOffsetX") && obj["motionEndOffsetX"].IsNumber()) {
            annotation.motion.endOffsetX = obj["motionEndOffsetX"].GetFloat();
        }
        if (obj.HasMember("motionEndOffsetY") && obj["motionEndOffsetY"].IsNumber()) {
            annotation.motion.endOffsetY = obj["motionEndOffsetY"].GetFloat();
        }
        if (obj.HasMember("motionTravelTime_ms") && obj["motionTravelTime_ms"].IsInt()) {
            annotation.motion.travelTime_ms = obj["motionTravelTime_ms"].GetInt();
        }
        if (obj.HasMember("projectileEndBehavior") && obj["projectileEndBehavior"].IsString()) {
            annotation.motion.endBehavior = stringToProjectileEndBehavior(obj["projectileEndBehavior"].GetString());
        }

        if (obj.HasMember("dropsPerSecond") && obj["dropsPerSecond"].IsNumber()) {
            annotation.motion.dropsPerSecond = obj["dropsPerSecond"].GetFloat();
        }
        if (obj.HasMember("dropRadius") && obj["dropRadius"].IsNumber()) {
            annotation.motion.dropRadius = obj["dropRadius"].GetFloat();
        }
        if (obj.HasMember("dropLifetime_ms") && obj["dropLifetime_ms"].IsInt()) {
            annotation.motion.dropLifetime_ms = obj["dropLifetime_ms"].GetInt();
        }

        if (obj.HasMember("worldMarkerType") && obj["worldMarkerType"].IsString()) {
            annotation.worldMarkerType = stringToWorldMarkerType(obj["worldMarkerType"].GetString());
        }
        if (obj.HasMember("markerHeight") && obj["markerHeight"].IsNumber()) {
            annotation.markerHeight = obj["markerHeight"].GetFloat();
        }

        annotation.positionValid = (annotation.anchorMode == AnnotationAnchor::World);

        annotations.push_back(annotation);
    }

    return true;
}

std::string toShareableJson(const std::vector<Annotation>& annotations, const std::string& encounterName) {
    rapidjson::Document doc;
    doc.SetObject();
    auto& alloc = doc.GetAllocator();

    doc.AddMember("version", 1, alloc);

    rapidjson::Value nameVal;
    nameVal.SetString(encounterName.c_str(),
                      static_cast<rapidjson::SizeType>(encounterName.length()), alloc);
    doc.AddMember("encounterName", nameVal, alloc);

    rapidjson::Value annotationsArray(rapidjson::kArrayType);

    for (const auto& annotation : annotations) {
        // Only manual, world-anchored annotations are shared
        if (annotation.deleted) continue;
        if (annotation.isAutoGenerated) continue;
        if (annotation.anchorMode != AnnotationAnchor::World) continue;

        rapidjson::Value obj(rapidjson::kObjectType);

        obj.AddMember("x", annotation.worldX, alloc);
        obj.AddMember("y", annotation.worldY, alloc);

        rapidjson::Value shapeStr;
        shapeStr.SetString(annotationShapeToString(annotation.shape), alloc);
        obj.AddMember("shape", shapeStr, alloc);

        rapidjson::Value dimsArray(rapidjson::kArrayType);
        for (int i = 0; i < 4; ++i) {
            dimsArray.PushBack(annotation.dimensions[i], alloc);
        }
        obj.AddMember("dimensions", dimsArray, alloc);

        // Point count (for Star shape)
        obj.AddMember("pointCount", annotation.pointCount, alloc);

        rapidjson::Value colorArray(rapidjson::kArrayType);
        for (int i = 0; i < 4; ++i) {
            colorArray.PushBack(annotation.color[i], alloc);
        }
        obj.AddMember("color", colorArray, alloc);

        obj.AddMember("startTime_ms", annotation.creationTimestamp_ms, alloc);

        rapidjson::Value durationStr;
        durationStr.SetString(annotationDurationToString(annotation.durationType), alloc);
        obj.AddMember("durationType", durationStr, alloc);
        obj.AddMember("durationMs", annotation.durationMs, alloc);

        annotationsArray.PushBack(obj, alloc);
    }

    doc.AddMember("annotations", annotationsArray, alloc);

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    return buffer.GetString();
}

std::vector<Annotation> fromShareableJson(const std::string& json) {
    std::vector<Annotation> result;

    rapidjson::Document doc;
    if (doc.Parse(json.c_str()).HasParseError() || !doc.IsObject()) {
        AWOW_LOG_ERROR("ANNOTATION", "Failed to parse shareable annotations JSON");
        return result;
    }

    if (!doc.HasMember("version") || !doc["version"].IsInt() || doc["version"].GetInt() != 1) {
        AWOW_LOG_ERROR("ANNOTATION", "Unsupported shareable annotations file version (expected v1)");
        return result;
    }

    if (!doc.HasMember("annotations") || !doc["annotations"].IsArray()) {
        return result;
    }

    const auto& annotationsArray = doc["annotations"].GetArray();

    for (const auto& obj : annotationsArray) {
        if (!obj.IsObject()) continue;

        Annotation annotation;
        annotation.id = 0;  // Caller assigns ID
        annotation.anchorMode = AnnotationAnchor::World;
        annotation.isAutoGenerated = false;
        annotation.positionValid = true;

        // Coordinates are required
        if (obj.HasMember("x") && obj.HasMember("y") &&
            obj["x"].IsNumber() && obj["y"].IsNumber()) {
            annotation.worldX = obj["x"].GetFloat();
            annotation.worldY = obj["y"].GetFloat();
            annotation.currentX = annotation.worldX;
            annotation.currentY = annotation.worldY;
        } else {
            continue;
        }

        if (obj.HasMember("shape") && obj["shape"].IsString()) {
            annotation.shape = stringToAnnotationShape(obj["shape"].GetString());
        }

        if (obj.HasMember("dimensions") && obj["dimensions"].IsArray()) {
            const auto& dimsArray = obj["dimensions"].GetArray();
            for (rapidjson::SizeType i = 0; i < dimsArray.Size() && i < 4; ++i) {
                if (dimsArray[i].IsNumber()) {
                    annotation.dimensions[i] = dimsArray[i].GetFloat();
                }
            }
        }

        // Point count (for Star shape)
        if (obj.HasMember("pointCount") && obj["pointCount"].IsUint()) {
            annotation.pointCount = static_cast<uint8_t>(obj["pointCount"].GetUint());
        }

        if (obj.HasMember("color") && obj["color"].IsArray()) {
            const auto& colorArray = obj["color"].GetArray();
            for (rapidjson::SizeType i = 0; i < colorArray.Size() && i < 4; ++i) {
                if (colorArray[i].IsNumber()) {
                    annotation.color[i] = colorArray[i].GetFloat();
                }
            }
        }

        if (obj.HasMember("startTime_ms") && obj["startTime_ms"].IsInt()) {
            annotation.creationTimestamp_ms = obj["startTime_ms"].GetInt();
        }

        if (obj.HasMember("durationType") && obj["durationType"].IsString()) {
            annotation.durationType = stringToAnnotationDuration(obj["durationType"].GetString());
        }

        if (obj.HasMember("durationMs") && obj["durationMs"].IsInt()) {
            annotation.durationMs = obj["durationMs"].GetInt();
        }

        result.push_back(annotation);
    }

    return result;
}

} // namespace AnnotationSerializer
