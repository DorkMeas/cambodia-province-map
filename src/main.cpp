#include <matplot/matplot.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

struct Point
{
    double latitude;
    double longitude;
};

struct Province
{
    std::string name;
    std::vector<std::vector<Point>> polygons;
};

std::string get_property_name(const json& properties)
{
    const std::vector<std::string> keys = {
        "name",
        "NAME",
        "Name",
        "province",
        "Province",
        "PROVINCE",
        "admin1Name",
        "ADM1_EN",
        "NAME_1",
        "shapeName",
        "ShapeName"
    };

    for (const auto& key : keys)
    {
        if (properties.contains(key) &&
            properties[key].is_string())
        {
            return properties[key].get<std::string>();
        }
    }

    return "Unknown Province";
}

std::vector<Point> parse_ring(const json& ring)
{
    std::vector<Point> points;

    if (!ring.is_array())
    {
        return points;
    }

    for (const auto& coordinate : ring)
    {
        if (!coordinate.is_array() ||
            coordinate.size() < 2)
        {
            continue;
        }

        if (!coordinate[0].is_number() ||
            !coordinate[1].is_number())
        {
            continue;
        }

        points.push_back({
            coordinate[1].get<double>(),
            coordinate[0].get<double>()
        });
    }

    return points;
}

std::vector<std::vector<Point>> parse_polygon(
    const json& coordinates)
{
    std::vector<std::vector<Point>> polygons;

    if (!coordinates.is_array())
    {
        return polygons;
    }

    for (const auto& ring : coordinates)
    {
        auto points = parse_ring(ring);

        if (points.size() >= 2)
        {
            polygons.push_back(std::move(points));
        }
    }

    return polygons;
}

std::vector<std::vector<Point>> parse_multipolygon(
    const json& coordinates)
{
    std::vector<std::vector<Point>> polygons;

    if (!coordinates.is_array())
    {
        return polygons;
    }

    for (const auto& polygon : coordinates)
    {
        auto parsed = parse_polygon(polygon);

        for (auto& ring : parsed)
        {
            polygons.push_back(std::move(ring));
        }
    }

    return polygons;
}

std::vector<Province> load_provinces(
    const std::string& filename)
{
    std::vector<Province> provinces;

    std::ifstream file(filename);

    if (!file)
    {
        std::cerr << "Could not open GeoJSON file:\n";
        std::cerr << filename << '\n';
        return provinces;
    }

    json data;

    try
    {
        file >> data;
    }
    catch (const json::parse_error& error)
    {
        std::cerr << "Invalid GeoJSON:\n";
        std::cerr << error.what() << '\n';
        return provinces;
    }

    if (!data.contains("features") ||
        !data["features"].is_array())
    {
        std::cerr
            << "GeoJSON does not contain a valid "
            << "features array.\n";

        return provinces;
    }

    for (const auto& feature : data["features"])
    {
        if (!feature.contains("geometry"))
        {
            continue;
        }

        const auto& geometry = feature["geometry"];

        if (!geometry.contains("type") ||
            !geometry.contains("coordinates"))
        {
            continue;
        }

        if (!geometry["type"].is_string())
        {
            continue;
        }

        Province province;

        if (feature.contains("properties") &&
            feature["properties"].is_object())
        {
            province.name =
                get_property_name(feature["properties"]);
        }
        else
        {
            province.name = "Unknown Province";
        }

        const std::string geometry_type =
            geometry["type"].get<std::string>();

        if (geometry_type == "Polygon")
        {
            province.polygons =
                parse_polygon(geometry["coordinates"]);
        }
        else if (geometry_type == "MultiPolygon")
        {
            province.polygons =
                parse_multipolygon(
                    geometry["coordinates"]
                );
        }
        else
        {
            continue;
        }

        if (!province.polygons.empty())
        {
            provinces.push_back(
                std::move(province)
            );
        }
    }

    return provinces;
}

Point calculate_centroid(
    const std::vector<Point>& polygon)
{
    if (polygon.empty())
    {
        return {};
    }

    double latitude = 0.0;
    double longitude = 0.0;

    for (const auto& point : polygon)
    {
        latitude += point.latitude;
        longitude += point.longitude;
    }

    return {
        latitude / polygon.size(),
        longitude / polygon.size()
    };
}

double polygon_area(
    const std::vector<Point>& polygon)
{
    if (polygon.size() < 3)
    {
        return 0.0;
    }

    double area = 0.0;

    for (size_t i = 0; i < polygon.size(); ++i)
    {
        const auto& current = polygon[i];
        const auto& next =
            polygon[(i + 1) % polygon.size()];

        area +=
            current.longitude * next.latitude -
            next.longitude * current.latitude;
    }

    return std::abs(area) / 2.0;
}

Point find_label_point(
    const Province& province)
{
    const std::vector<Point>* largest_polygon = nullptr;
    double largest_area = 0.0;

    for (const auto& polygon : province.polygons)
    {
        const double area =
            polygon_area(polygon);

        if (area > largest_area)
        {
            largest_area = area;
            largest_polygon = &polygon;
        }
    }

    if (largest_polygon)
    {
        return calculate_centroid(
            *largest_polygon
        );
    }

    return {};
}

void draw_province(
    const Province& province)
{
    using namespace matplot;

    for (const auto& polygon : province.polygons)
    {
        if (polygon.size() < 2)
        {
            continue;
        }

        std::vector<double> latitude;
        std::vector<double> longitude;

        latitude.reserve(polygon.size());
        longitude.reserve(polygon.size());

        for (const auto& point : polygon)
        {
            latitude.push_back(point.latitude);
            longitude.push_back(point.longitude);
        }

        geoplot(
            latitude,
            longitude,
            "k-"
        );
    }
}

int main()
{
    using namespace matplot;

    const std::string filename =
        "data/CambodiaProvinceBoundaries.geojson";

    const auto provinces =
        load_provinces(filename);

    if (provinces.empty())
    {
        std::cerr
            << "No province data found.\n";

        return 1;
    }

    std::cout
        << "Loaded provinces: "
        << provinces.size()
        << '\n';

    figure();

    for (const auto& province : provinces)
    {
        draw_province(province);

        const auto label =
            find_label_point(province);

        if (label.latitude != 0.0 &&
            label.longitude != 0.0)
        {
            text(
                label.longitude,
                label.latitude,
                province.name
            );
        }
    }

    geolimits(
        9.5,
        15.5,
        102.0,
        108.0
    );

    title(
        "Cambodia Province Map"
    );

    xlabel("Longitude");
    ylabel("Latitude");

    grid(on);

    show();

    return 0;
}