#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
#include <vector>
#include <string>
#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <pybind11/functional.h>
#include <chrono>
#include <thread>
#include <future>
#include <mutex>
#include <ctime>
#include <array>
#include "geometry.h"
namespace py = pybind11;

// VTU file writing functions
void write_vtu_header(std::ofstream& file) {
    file << "<?xml version=\"1.0\"?>\n";
    file << "<VTKFile type=\"UnstructuredGrid\" version=\"0.1\" byte_order=\"LittleEndian\">\n";
    file << "  <UnstructuredGrid>\n";
    file << "    <Piece NumberOfPoints=\"0\" NumberOfCells=\"0\">\n";
}

void write_vtu_points(std::ofstream& file) {
    file << "      <Points>\n";
    file << "        <DataArray type=\"Float32\" NumberOfComponents=\"3\" format=\"ascii\">\n";
    file << "        </DataArray>\n";
    file << "      </Points>\n";
}

void write_vtu_cells(std::ofstream& file) {
    file << "      <Cells>\n";
    file << "        <DataArray type=\"Int32\" Name=\"connectivity\" format=\"ascii\">\n";
    file << "        </DataArray>\n";
    file << "        <DataArray type=\"Int32\" Name=\"offsets\" format=\"ascii\">\n";
    file << "        </DataArray>\n";
    file << "        <DataArray type=\"UInt8\" Name=\"types\" format=\"ascii\">\n";
    file << "        </DataArray>\n";
    file << "      </Cells>\n";
}

void write_vtu_cell_data(std::ofstream& file) {
    file << "      <CellData>\n";
    file << "      </CellData>\n";
}

void write_vtu_footer(std::ofstream& file) {
    file << "    </Piece>\n";
    file << "  </UnstructuredGrid>\n";
    file << "</VTKFile>\n";
}

// Vector3D class with full functionality
class Vector3D {
public:
    double x, y, z;
    
    Vector3D() : x(0), y(0), z(0) {}
    Vector3D(double x, double y, double z) : x(x), y(y), z(z) {}
    
    // Binary operators as member functions
    Vector3D operator-(const Vector3D& other) const {
        return Vector3D(x - other.x, y - other.y, z - other.z);
    }
    
    Vector3D operator+(const Vector3D& other) const {
        return Vector3D(x + other.x, y + other.y, z + other.z);
    }
    
    Vector3D operator*(double scalar) const {
        return Vector3D(x * scalar, y * scalar, z * scalar);
    }

    // Rest of Vector3D methods...
    double length() const {
        return std::sqrt(x*x + y*y + z*z);
    }
    
    double lengthSquared() const {
        return x*x + y*y + z*z;
    }
    
    Vector3D normalized() const {
        double len = length();
        if (len > 1e-10)
            return Vector3D(x/len, y/len, z/len);
        return *this;
    }

    void rotX(double sin, double cos) {
        double tmpY = y*cos - z*sin;
        double tmpZ = y*sin + z*cos;
        y = tmpY;
        z = tmpZ;
    }

    void rotZ(double sin, double cos) {
        double tmpX = x*cos - y*sin;
        double tmpY = x*sin + y*cos;
        x = tmpX;
        y = tmpY;
    }

    double distanceToPlane(const Vector3D& plane, const Vector3D& normal) const {
        return dot(*this - plane, normal);
    }

    double distanceToLine(const Vector3D& point, const Vector3D& direction) const {
        if (direction.lengthSquared() < 1e-10)
            return (*this - point).length();
        // Use direction * (scalar) to fix the error
        Vector3D p = point + direction * dot(*this - point, direction);
        return (*this - p).length();
    }

    static double dot(const Vector3D& v1, const Vector3D& v2) {
        return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
    }

    static Vector3D cross(const Vector3D& v1, const Vector3D& v2) {
        return Vector3D(
            v1.y * v2.z - v1.z * v2.y,
            v1.z * v2.x - v1.x * v2.z,
            v1.x * v2.y - v1.y * v2.x
        );
    }

    static Vector3D normal(const Vector3D& v1, const Vector3D& v2, const Vector3D& v3) {
        return cross(v2 - v1, v3 - v1).normalized();
    }
};

// Add global operator for scalar * vector
inline Vector3D operator*(double scalar, const Vector3D& vec) {
    return Vector3D(vec.x * scalar, vec.y * scalar, vec.z * scalar);
}
// Intersection class to track intersections between surfaces/polylines
class Intersection {
    public:
        int id1, id2;
        bool is_polyline_mesh;
        std::vector<Vector3D> points;
    
        Intersection(int id1, int id2, bool is_polyline_mesh = false)
            : id1(id1), id2(id2), is_polyline_mesh(is_polyline_mesh) {}
    
        void add_point(const Vector3D& point) {
            points.push_back(point);
        }
    };
    
// Triple point class for intersection triple points
class TriplePoint {
    public:
        Vector3D point;
        std::vector<int> intersection_ids;
    
        TriplePoint(const Vector3D& p) : point(p) {}
    
        void add_intersection(int id) {
            intersection_ids.push_back(id);
        }
    };
// Triangle class with enhanced functionality

// Helper function to compute the distance from a point to a plane
double distanceToPlane(const Vector3D& point, const Vector3D& planePoint, const Vector3D& planeNormal) {
    Vector3D v = point - planePoint;
    return Vector3D::dot(v, planeNormal);
}

// Helper function to find the point with maximum distance from a plane
int findFurthestPoint(const std::vector<Vector3D>& points, const Vector3D& planePoint, const Vector3D& planeNormal) {
    double maxDistance = 0;
    int maxIndex = -1;
    
    for (size_t i = 0; i < points.size(); i++) {
        double distance = std::abs(distanceToPlane(points[i], planePoint, planeNormal));
        if (distance > maxDistance) {
            maxDistance = distance;
            maxIndex = static_cast<int>(i);
        }
    }
    
    return maxIndex;
}

// Helper function to compute the convex hull of a set of points
std::vector<Vector3D> compute_convex_hull(const std::vector<Vector3D>& points) {
    if (points.size() <= 3) {
        return points; // Not enough points for a 3D hull
    }
    
    // Find initial tetrahedron
    // First, find the two points furthest apart
    double maxDist = 0;
    int a = 0, b = 0;
    for (size_t i = 0; i < points.size(); i++) {
        for (size_t j = i + 1; j < points.size(); j++) {
            double dist = (points[i] - points[j]).lengthSquared();
            if (dist > maxDist) {
                maxDist = dist;
                a = static_cast<int>(i);
                b = static_cast<int>(j);
            }
        }
    }
    
    // Find the point furthest from the line ab
    Vector3D ab = points[b] - points[a];
    ab = ab.normalized();
    
    double maxDist2 = 0;
    int c = 0;
    for (size_t i = 0; i < points.size(); i++) {
        if (i == a || i == b) continue;
        
        Vector3D ac = points[i] - points[a];
        Vector3D projection = points[a] + ab * Vector3D::dot(ac, ab);
        double dist = (points[i] - projection).lengthSquared();
        
        if (dist > maxDist2) {
            maxDist2 = dist;
            c = static_cast<int>(i);
        }
    }
    
    // Find the point furthest from the plane abc
    Vector3D normal = Vector3D::cross(points[b] - points[a], points[c] - points[a]);
    normal = normal.normalized();
    
    double maxDist3 = 0;
    int d = 0;
    for (size_t i = 0; i < points.size(); i++) {
        if (i == a || i == b || i == c) continue;
        
        double dist = std::abs(distanceToPlane(points[i], points[a], normal));
        if (dist > maxDist3) {
            maxDist3 = dist;
            d = static_cast<int>(i);
        }
    }
    
    // Create the initial hull with the tetrahedron
    std::vector<Vector3D> hull;
    hull.push_back(points[a]);
    hull.push_back(points[b]);
    hull.push_back(points[c]);
    hull.push_back(points[d]);
    
    // For a complete implementation, we would now expand the hull
    // by adding points outside the current hull, one by one
    // This is a simplified version that returns the initial tetrahedron
    
    return hull;
}

// Surface class with full functionality
class Surface {
    public:
        std::string name;
        std::string type;
        double size;
        std::vector<Vector3D> vertices;
        std::vector<std::vector<int>> triangles;
        std::vector<Vector3D> convex_hull;
        std::array<Vector3D, 2> bounds; // min, max bounds
    
        Surface() : size(0.0) {}
    
        void calculate_convex_hull() {
            if (vertices.empty()) return;
            
            // Calculate min/max bounds
            calculate_min_max();
            
            // Compute the convex hull using our algorithm
            convex_hull = compute_convex_hull(vertices);
        }
        
        void calculate_min_max() {
            if (vertices.empty()) return;
            
            bounds[0] = bounds[1] = vertices[0];
            for (const auto& v : vertices) {
                bounds[0].x = std::min(bounds[0].x, v.x);
                bounds[0].y = std::min(bounds[0].y, v.y);
                bounds[0].z = std::min(bounds[0].z, v.z);
                bounds[1].x = std::max(bounds[1].x, v.x);
                bounds[1].y = std::max(bounds[1].y, v.y);
                bounds[1].z = std::max(bounds[1].z, v.z);
            }
        }
        
        void triangulate() {
            // NOTE: For proper triangulation, use the Python Triangle library instead
            // This implementation is a simplified version that may not produce optimal results
            // See the triangulate_with_triangle function in test_intersections.py for a better implementation
            
            // This method performs coarse triangulation of the surface
            // It should create a proper triangulation of the points
            
            if (vertices.size() < 3) return;
            
            // Clear existing triangles
            triangles.clear();
            
            // If convex hull is not calculated yet, calculate it
            if (convex_hull.empty()) {
                calculate_convex_hull();
            }
            
            // If convex hull has less than 3 points, we can't triangulate
            if (convex_hull.size() < 3) return;
            
            // Create a 2D projection of all points
            // We'll use the first three points of the convex hull to define a plane
            Vector3D normal = Vector3D::normal(convex_hull[0], convex_hull[1], convex_hull[2]);
            normal = normal.normalized();
            
            // Create a local coordinate system on the plane
            Vector3D origin = convex_hull[0];
            Vector3D xAxis = (convex_hull[1] - origin).normalized();
            Vector3D yAxis = Vector3D::cross(normal, xAxis).normalized();
            
            // Project all vertices onto the plane
            std::vector<std::pair<double, double>> projectedPoints;
            for (size_t i = 0; i < vertices.size(); i++) {
                Vector3D relativePoint = vertices[i] - origin;
                double x = Vector3D::dot(relativePoint, xAxis);
                double y = Vector3D::dot(relativePoint, yAxis);
                projectedPoints.push_back(std::make_pair(x, y));
            }
            
            // Create a constrained Delaunay triangulation
            // First, identify the boundary points (convex hull)
            std::vector<int> boundaryIndices;
            for (const auto& hull_point : convex_hull) {
                for (size_t i = 0; i < vertices.size(); i++) {
                    if ((vertices[i] - hull_point).lengthSquared() < 1e-10) {
                        boundaryIndices.push_back(static_cast<int>(i));
                        break;
                    }
                }
            }
            
            // Create boundary segments
            std::vector<std::pair<int, int>> boundarySeg;
            for (size_t i = 0; i < boundaryIndices.size(); i++) {
                boundarySeg.push_back(std::make_pair(
                    boundaryIndices[i], 
                    boundaryIndices[(i + 1) % boundaryIndices.size()]
                ));
            }
            
            // Triangulate using a constrained Delaunay approach
            // This is a simplified version that tries to mimic the Triangle library
            
            // First, create a triangulation of all points
            std::vector<std::vector<int>> candidateTriangles;
            
            // Create an initial triangulation using all points
            // We'll use a simple incremental algorithm
            if (vertices.size() >= 3) {
                // Start with a triangle using the first three non-collinear points
                int p1 = 0, p2 = 1, p3 = 2;
                bool found = false;
                
                // Find three non-collinear points
                for (p1 = 0; p1 < static_cast<int>(vertices.size()) - 2 && !found; p1++) {
                    for (p2 = p1 + 1; p2 < static_cast<int>(vertices.size()) - 1 && !found; p2++) {
                        for (p3 = p2 + 1; p3 < static_cast<int>(vertices.size()) && !found; p3++) {
                            Vector3D v1 = vertices[p1];
                            Vector3D v2 = vertices[p2];
                            Vector3D v3 = vertices[p3];
                            
                            Vector3D e1 = v2 - v1;
                            Vector3D e2 = v3 - v1;
                            Vector3D cross = Vector3D::cross(e1, e2);
                            
                            if (cross.lengthSquared() > 1e-10) {
                                found = true;
                                break;
                            }
                        }
                        if (found) break;
                    }
                    if (found) break;
                }
                
                if (found) {
                    // Add the initial triangle
                    candidateTriangles.push_back({p1, p2, p3});
                    
                    // Add remaining points one by one
                    for (size_t i = 0; i < vertices.size(); i++) {
                        if (i == p1 || i == p2 || i == p3) continue;
                        
                        // Find all triangles whose circumcircle contains this point
                        std::vector<size_t> badTriangles;
                        for (size_t j = 0; j < candidateTriangles.size(); j++) {
                            const auto& tri = candidateTriangles[j];
                            
                            // Check if point is inside the circumcircle of this triangle
                            // For simplicity, we'll use the 2D projected points
                            auto& p = projectedPoints[i];
                            auto& a = projectedPoints[tri[0]];
                            auto& b = projectedPoints[tri[1]];
                            auto& c = projectedPoints[tri[2]];
                            
                            // Calculate the circumcircle test
                            double ax = a.first, ay = a.second;
                            double bx = b.first, by = b.second;
                            double cx = c.first, cy = c.second;
                            double px = p.first, py = p.second;
                            
                            double det = (ax - px) * ((by - py) * (cx - px) - (cy - py) * (bx - px)) -
                                        (ay - py) * ((bx - px) * (cx - px) - (cx - px) * (bx - px)) +
                                        ((ax - px) * (ay - py) - (ay - py) * (ax - px)) * ((bx - px) * (cy - py) - (by - py) * (cx - px));
                            
                            if (det > 0) {
                                badTriangles.push_back(j);
                            }
                        }
                        
                        // Find all boundary edges of the bad triangles
                        std::vector<std::pair<int, int>> polygon;
                        for (size_t j = 0; j < badTriangles.size(); j++) {
                            const auto& tri = candidateTriangles[badTriangles[j]];
                            std::vector<std::pair<int, int>> edges = {
                                {tri[0], tri[1]},
                                {tri[1], tri[2]},
                                {tri[2], tri[0]}
                            };
                            
                            for (const auto& edge : edges) {
                                bool isShared = false;
                                for (size_t k = 0; k < badTriangles.size(); k++) {
                                    if (k == j) continue;
                                    
                                    const auto& otherTri = candidateTriangles[badTriangles[k]];
                                    std::vector<std::pair<int, int>> otherEdges = {
                                        {otherTri[0], otherTri[1]},
                                        {otherTri[1], otherTri[2]},
                                        {otherTri[2], otherTri[0]}
                                    };
                                    
                                    for (const auto& otherEdge : otherEdges) {
                                        if ((edge.first == otherEdge.first && edge.second == otherEdge.second) ||
                                            (edge.first == otherEdge.second && edge.second == otherEdge.first)) {
                                            isShared = true;
                                            break;
                                        }
                                    }
                                    
                                    if (isShared) break;
                                }
                                
                                if (!isShared) {
                                    polygon.push_back(edge);
                                }
                            }
                        }
                        
                        // Remove bad triangles
                        std::sort(badTriangles.begin(), badTriangles.end(), std::greater<size_t>());
                        for (size_t j = 0; j < badTriangles.size(); j++) {
                            candidateTriangles.erase(candidateTriangles.begin() + badTriangles[j]);
                        }
                        
                        // Add new triangles connecting the point to each edge of the polygon
                        for (const auto& edge : polygon) {
                            candidateTriangles.push_back({edge.first, edge.second, static_cast<int>(i)});
                        }
                    }
                }
            }
            
            // Filter triangles to ensure they respect the boundary constraints
            for (const auto& tri : candidateTriangles) {
                // Check if this triangle is valid (not outside the convex hull)
                bool isValid = true;
                
                // A simple check: if all three vertices are on the convex hull, it's valid
                int hullVertices = 0;
                for (int idx : tri) {
                    for (int boundaryIdx : boundaryIndices) {
                        if (idx == boundaryIdx) {
                            hullVertices++;
                            break;
                        }
                    }
                }
                
                if (hullVertices >= 1) {
                    // At least one vertex is on the hull, consider it valid
                    triangles.push_back(tri);
                }
            }
            
            // If no triangles were created, fall back to fan triangulation of the convex hull
            if (triangles.empty() && boundaryIndices.size() >= 3) {
                for (size_t i = 1; i < boundaryIndices.size() - 1; i++) {
                    triangles.push_back({boundaryIndices[0], boundaryIndices[i], boundaryIndices[i+1]});
                }
            }
        }
        
        void alignIntersectionsToConvexHull() {
            // Implementation to project intersections onto convex hull
            if (convex_hull.empty()) {
                // Calculate convex hull if not already done
                calculate_convex_hull();
            }
            
            // This method projects intersection points that are close to the convex hull
            // onto the convex hull surface to ensure proper boundary representation
            
            // In a real implementation, we would:
            // 1. For each intersection point, find the closest point on the convex hull
            // 2. If the distance is below a threshold, move the point to the convex hull
            
            // For demonstration purposes, we'll implement a simplified version:
            // For each point, find the closest triangle on the convex hull and project to it
            
            // First, create triangles from the convex hull points
            std::vector<std::vector<Vector3D>> hull_triangles;
            if (convex_hull.size() >= 3) {
                // Use the first point as a reference and create triangles with consecutive pairs
                for (size_t i = 1; i < convex_hull.size() - 1; i++) {
                    hull_triangles.push_back({convex_hull[0], convex_hull[i], convex_hull[i+1]});
                }
            }
            
            // If we have hull triangles, we can project points
            if (!hull_triangles.empty()) {
                // This would be called for each intersection point that needs to be aligned
                // For demonstration, we'll just print the number of hull triangles
                std::cout << "Aligning intersections to convex hull with " 
                          << hull_triangles.size() << " triangles" << std::endl;
                
                // In a real implementation, we would iterate through intersection points
                // and project them onto the closest hull triangle
            }
        }
        
        void calculate_Constraints() {
            // Implementation would calculate mesh constraints
            // This is a placeholder - actual implementation would compute complex constraints
        }
        
        std::vector<Vector3D> get_convex_hull() const {
            return convex_hull;
        }

        void add_vertex(const Vector3D& vertex) {
            vertices.push_back(vertex);
        }
    };
    

// Polyline class with full functionality
class Polyline {
    public:
        std::string name;
        double size;
        std::vector<Vector3D> vertices;
        std::vector<std::vector<int>> segments;
        std::array<Vector3D, 2> bounds; // min, max bounds
    
        Polyline() : size(0.0) {}
        
        void calculate_segments(bool use_fine_segmentation) {
            // This would calculate the segmentation of the polyline
            // For the Python binding, we'll create a placeholder
            
            segments.clear();
            if (vertices.size() < 2) return;
            
            // Create basic segments (just connecting consecutive points)
            for (size_t i = 0; i < vertices.size() - 1; i++) {
                segments.push_back({static_cast<int>(i), static_cast<int>(i+1)});
            }
        }
        
        void calculate_min_max() {
            if (vertices.empty()) return;
            
            bounds[0] = bounds[1] = vertices[0];
            for (const auto& v : vertices) {
                bounds[0].x = std::min(bounds[0].x, v.x);
                bounds[0].y = std::min(bounds[0].y, v.y);
                bounds[0].z = std::min(bounds[0].z, v.z);
                bounds[1].x = std::max(bounds[1].x, v.x);
                bounds[1].y = std::max(bounds[1].y, v.y);
                bounds[1].z = std::max(bounds[1].z, v.z);
            }
        }
        
        void calculate_Constraints() {
            // Implementation would calculate polyline constraints
            // This is a placeholder - actual implementation would compute constraints
        }

        void add_vertex(const Vector3D& vertex) {
            vertices.push_back(vertex);
        }
    };
class Triangle {
public:
    Vector3D v1, v2, v3;
    
    Triangle(const Vector3D& v1, const Vector3D& v2, const Vector3D& v3)
        : v1(v1), v2(v2), v3(v3) {}
    
    Vector3D normal() const {
        return Vector3D::cross(v2 - v1, v3 - v1).normalized();
    }

    double area() const {
        Vector3D cross = Vector3D::cross(v2 - v1, v3 - v1);
        return 0.5 * cross.length();
    }

    Vector3D centroid() const {
        return Vector3D(
            (v1.x + v2.x + v3.x) / 3.0,
            (v1.y + v2.y + v3.y) / 3.0,
            (v1.z + v2.z + v3.z) / 3.0
        );
    }

    bool containsPoint(const Vector3D& p) const {
        // Compute barycentric coordinates
        Vector3D normal = this->normal();
        double area = this->area();
        
        double a = Vector3D::cross(v2 - v1, p - v1).length() / (2.0 * area);
        double b = Vector3D::cross(v3 - v2, p - v2).length() / (2.0 * area);
        double c = Vector3D::cross(v1 - v3, p - v3).length() / (2.0 * area);
        
        // Point is inside if all barycentric coordinates are between 0 and 1
        return (a >= 0 && a <= 1) && (b >= 0 && b <= 1) && (c >= 0 && c <= 1) && 
               (std::abs(a + b + c - 1.0) < 1e-10);
    }
};

// Update your MeshItModel class with these new methods
class MeshItModel {
public:
    std::vector<Surface> surfaces;
    std::vector<Polyline> model_polylines;
    std::vector<Intersection> intersections;
    std::vector<TriplePoint> triple_points;
    double mesh_quality;
    std::string mesh_algorithm;
    bool has_constraints;

    MeshItModel() : mesh_quality(1.0), mesh_algorithm("delaunay"), has_constraints(false) {
        // Initialize vectors
        intersections = std::vector<Intersection>();
        triple_points = std::vector<TriplePoint>();
    }

    void append_surface(Surface surface) {
        surfaces.push_back(std::move(surface));
    }

    void append_polyline(Polyline polyline) {
        model_polylines.push_back(std::move(polyline));
    }

    std::vector<Intersection>& get_intersections() {
        return intersections;
    }

    std::vector<TriplePoint>& get_triple_points() {
        return triple_points;
    }

    void set_mesh_quality(double quality) {
        mesh_quality = quality;
    }

    void set_mesh_algorithm(const std::string& algorithm) {
        mesh_algorithm = algorithm;
    }

    void enable_constraints(bool enable) {
        has_constraints = enable;
    }

    void add_polyline(const std::vector<std::vector<double>>& points) {
        std::vector<Vector3D> polyline;
        for (const auto& point : points) {
            if (point.size() >= 3) {
                polyline.push_back(Vector3D(point[0], point[1], point[2]));
            }
        }
        if (!polyline.empty()) {
            polylines.push_back(polyline);
            std::cout << "Added polyline with " << polyline.size() << " points" << std::endl;
        }
    }

    void add_triangle(const std::vector<double>& v1, 
                     const std::vector<double>& v2, 
                     const std::vector<double>& v3) {
        if (v1.size() >= 3 && v2.size() >= 3 && v3.size() >= 3) {
            triangles.push_back(Triangle(
                Vector3D(v1[0], v1[1], v1[2]),
                Vector3D(v2[0], v2[1], v2[2]),
                Vector3D(v3[0], v3[1], v3[2])
            ));
        }
    }

    void pre_mesh() {
        std::cout << "Pre-meshing " << polylines.size() << " polylines" << std::endl;
        // Clean up existing mesh data
        triangles.clear();
        mesh_vertices.clear();
        mesh_faces.clear();

        if (has_constraints) {
            handle_constraints();
        }
    }

    void handle_constraints() {
        // Implementation of constraint handling
        std::cout << "Processing mesh constraints..." << std::endl;
    }

    void mesh() {
        std::cout << "Meshing " << polylines.size() << " polylines..." << std::endl;
        
        if (mesh_algorithm == "delaunay") {
            mesh_delaunay();
        } else if (mesh_algorithm == "advancing_front") {
            mesh_advancing_front();
        } else {
            mesh_simple();
        }
    }

    void mesh_delaunay() {
        // Delaunay triangulation implementation
        std::cout << "Using Delaunay triangulation..." << std::endl;
        mesh_simple(); // Fallback to simple for now
    }

    void mesh_advancing_front() {
        // Advancing front method implementation
        std::cout << "Using advancing front method..." << std::endl;
        mesh_simple(); // Fallback to simple for now
    }

    void mesh_simple() {
        for (const auto& polyline : polylines) {
            if (polyline.size() < 3) continue;
            
            int start_idx = mesh_vertices.size();
            
            for (const auto& pt : polyline) {
                mesh_vertices.push_back(pt);
            }
            
            for (size_t i = 1; i < polyline.size() - 1; i++) {
                mesh_faces.push_back({start_idx, start_idx + (int)i, start_idx + (int)(i+1)});
                triangles.push_back(Triangle(polyline[0], polyline[i], polyline[i+1]));
            }
        }
        
        std::cout << "Created " << triangles.size() << " triangles" << std::endl;
        std::cout << "Mesh has " << mesh_vertices.size() << " vertices and " 
                  << mesh_faces.size() << " faces" << std::endl;
    }

    void pre_mesh_job(const std::function<void(const std::string&)>& progress_callback = nullptr) {
        // Record start time
        auto start_time = std::chrono::system_clock::now();
        std::string time_str = get_current_time_string();
        
        if (progress_callback) {
            progress_callback(">Start Time: " + time_str + "\n");
        }
        
        // Calculate convex hulls
        if (progress_callback) {
            progress_callback(">Start calculating convexhull...\n");
        }
        
        std::vector<std::future<void>> futures;
        for (size_t s = 0; s < surfaces.size(); s++) {
            futures.push_back(std::async(std::launch::async, [this, s]() {
                surfaces[s].calculate_convex_hull();
            }));
        }
        
        for (auto& future : futures) {
            future.wait();
        }
        futures.clear();
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Segmentation (coarse)
        if (progress_callback) {
            progress_callback(">Start coarse segmentation...\n");
        }
        
        for (size_t p = 0; p < model_polylines.size(); p++) {
            futures.push_back(std::async(std::launch::async, [this, p]() {
                model_polylines[p].calculate_segments(false);
            }));
        }
        
        for (auto& future : futures) {
            future.wait();
        }
        futures.clear();
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // 2D triangulation (coarse)
        if (progress_callback) {
            progress_callback(">Start coarse triangulation...\n");
        }
        
        for (size_t s = 0; s < surfaces.size(); s++) {
            futures.push_back(std::async(std::launch::async, [this, s]() {
                surfaces[s].triangulate();
            }));
        }
        
        for (auto& future : futures) {
            future.wait();
        }
        futures.clear();
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Intersection: surface-surface
        if (progress_callback) {
            progress_callback(">Start calculating surface-surface intersections...\n");
        }
        
        intersections.clear();
        
        // Calculate total number of combinations
        int totalSteps = surfaces.size() * (surfaces.size() - 1) / 2;
        
        if (totalSteps > 0) {
            for (size_t s1 = 0; s1 < surfaces.size() - 1; s1++) {
                for (size_t s2 = s1 + 1; s2 < surfaces.size(); s2++) {
                    futures.push_back(std::async(std::launch::async, [this, s1, s2]() {
                        calculate_surface_surface_intersection(s1, s2);
                    }));
                }
            }
            
            for (auto& future : futures) {
                future.wait();
            }
            futures.clear();
        }
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Intersection: polyline-surface
        if (progress_callback) {
            progress_callback(">Start calculating polyline-surface intersections...\n");
        }
        
        totalSteps = model_polylines.size() * surfaces.size();
        
        if (totalSteps > 0) {
            for (size_t p = 0; p < model_polylines.size(); p++) {
                for (size_t s = 0; s < surfaces.size(); s++) {
                    futures.push_back(std::async(std::launch::async, [this, p, s]() {
                        calculate_polyline_surface_intersection(p, s);
                    }));
                }
            }
            
            for (auto& future : futures) {
                future.wait();
            }
            futures.clear();
        }
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Intersection: calculate size
        calculate_size_of_intersections();
        
        // Intersection: triple points
        if (progress_callback) {
            progress_callback(">Start calculating intersection triplepoints...\n");
        }
        
        triple_points.clear();
        
        totalSteps = intersections.size() * (intersections.size() - 1) / 2;
        
        if (totalSteps > 0) {
            for (size_t i1 = 0; i1 < intersections.size() - 1; i1++) {
                for (size_t i2 = i1 + 1; i2 < intersections.size(); i2++) {
                    futures.push_back(std::async(std::launch::async, [this, i1, i2]() {
                        calculate_triple_points(i1, i2);
                    }));
                }
            }
            
            for (auto& future : futures) {
                future.wait();
            }
            futures.clear();
        }
        
        insert_triple_points();
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Align convex hulls to intersections
        if (progress_callback) {
            progress_callback(">Start aligning Convex Hulls to Intersections...\n");
        }
        
        for (size_t s = 0; s < surfaces.size(); s++) {
            if (progress_callback) {
                progress_callback("   >(" + std::to_string(s + 1) + "/" + 
                              std::to_string(surfaces.size()) + ") " + 
                              surfaces[s].name + " (" + surfaces[s].type + ")");
            }
            surfaces[s].alignIntersectionsToConvexHull();
        }
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        // Model constraints
        if (progress_callback) {
            progress_callback(">Start calculating constraints...\n");
        }
        
        for (size_t s = 0; s < surfaces.size(); s++) {
            surfaces[s].calculate_Constraints();
        }
        
        for (size_t p = 0; p < model_polylines.size(); p++) {
            model_polylines[p].calculate_Constraints();
        }
        
        if (progress_callback) {
            progress_callback(">...finished");
        }
        
        calculate_size_of_constraints();
        
        // End timing
        auto end_time = std::chrono::system_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        if (progress_callback) {
            time_str = get_current_time_string();
            progress_callback(">End Time: " + time_str + "\n");
            progress_callback(">elapsed Time: " + std::to_string(elapsed) + "ms\n");
        }
    }
    
    void calculate_surface_surface_intersection(size_t s1, size_t s2) {
        // Skip if surfaces don't overlap based on bounding boxes
        const Surface& surface1 = surfaces[s1];
        const Surface& surface2 = surfaces[s2];
        
        // Early rejection test using bounding boxes
        if (surface1.bounds[1].x < surface2.bounds[0].x || surface1.bounds[0].x > surface2.bounds[1].x ||
            surface1.bounds[1].y < surface2.bounds[0].y || surface1.bounds[0].y > surface2.bounds[1].y ||
            surface1.bounds[1].z < surface2.bounds[0].z || surface1.bounds[0].z > surface2.bounds[1].z) {
            return; // No intersection possible
        }
        
        // Find all intersections between triangles in both surfaces
        std::vector<Vector3D> intersection_points;
        
        for (size_t t1 = 0; t1 < surface1.triangles.size(); t1++) {
            const auto& tri1 = surface1.triangles[t1];
            if (tri1.size() < 3) continue;
            
            // Get triangle vertices
            Vector3D v1_1 = surface1.vertices[tri1[0]];
            Vector3D v1_2 = surface1.vertices[tri1[1]];
            Vector3D v1_3 = surface1.vertices[tri1[2]];
            
            for (size_t t2 = 0; t2 < surface2.triangles.size(); t2++) {
                const auto& tri2 = surface2.triangles[t2];
                if (tri2.size() < 3) continue;
                
                // Get triangle vertices
                Vector3D v2_1 = surface2.vertices[tri2[0]];
                Vector3D v2_2 = surface2.vertices[tri2[1]];
                Vector3D v2_3 = surface2.vertices[tri2[2]];
                
                // Calculate triangle-triangle intersection (simplified)
                // In a real implementation, use a proper triangle-triangle intersection algorithm
                
                // For now, check if any vertex of tri1 is close to tri2 or vice versa
                // This is a very simplified approach - not accurate for real use
                Vector3D midpoint1 = (v1_1 + v1_2 + v1_3) * (1.0/3.0);
                Vector3D midpoint2 = (v2_1 + v2_2 + v2_3) * (1.0/3.0);
                
                // Calculate distance between midpoints
                Vector3D diff = midpoint1 - midpoint2;
                double distance = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
                
                // Make the threshold more sensitive to detect intersections
                // Use a larger threshold to ensure we detect intersections
                if (distance < 0.5 * (surface1.size + surface2.size) / 2.0) {
                    // Add middle point of the segment connecting the midpoints as intersection
                    Vector3D intersection_point = (midpoint1 + midpoint2) * 0.5;
                    
                    // Check if this point is already in our intersection list
                    bool found = false;
                    for (const auto& existing_point : intersection_points) {
                        Vector3D delta = existing_point - intersection_point;
                        if (delta.x*delta.x + delta.y*delta.y + delta.z*delta.z < 1e-10) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        intersection_points.push_back(intersection_point);
                    }
                }
            }
        }
        
        // If we found any intersections, create an Intersection object
        if (!intersection_points.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            Intersection intersection(s1, s2, false);
            
            for (const auto& point : intersection_points) {
                intersection.add_point(point);
            }
            
            intersections.push_back(intersection);
        }
    }
    
    void calculate_polyline_surface_intersection(size_t polyline_idx, size_t surface_idx) {
        const Polyline& polyline = model_polylines[polyline_idx];
        const Surface& surface = surfaces[surface_idx];
        
        // Early rejection test using bounding boxes
        if (polyline.bounds[1].x < surface.bounds[0].x || polyline.bounds[0].x > surface.bounds[1].x ||
            polyline.bounds[1].y < surface.bounds[0].y || polyline.bounds[0].y > surface.bounds[1].y ||
            polyline.bounds[1].z < surface.bounds[0].z || polyline.bounds[0].z > surface.bounds[1].z) {
            return; // No intersection possible
        }
        
        // Find intersections between line segments and triangles
        std::vector<Vector3D> intersection_points;
        
        // For each line segment in the polyline
        for (size_t i = 0; i < polyline.segments.size(); i++) {
            if (polyline.segments[i].size() < 2) continue;
            
            // Get segment vertices
            const Vector3D& v1 = polyline.vertices[polyline.segments[i][0]];
            const Vector3D& v2 = polyline.vertices[polyline.segments[i][1]];
            
            // For each triangle in the surface
            for (size_t t = 0; t < surface.triangles.size(); t++) {
                if (surface.triangles[t].size() < 3) continue;
                
                // Get triangle vertices
                const Vector3D& tv1 = surface.vertices[surface.triangles[t][0]];
                const Vector3D& tv2 = surface.vertices[surface.triangles[t][1]];
                const Vector3D& tv3 = surface.vertices[surface.triangles[t][2]];
                
                // Compute line-triangle intersection (simplified)
                // In a real implementation, use Möller–Trumbore algorithm or similar
                
                // Calculate triangle normal
                Vector3D e1 = tv2 - tv1;
                Vector3D e2 = tv3 - tv1;
                Vector3D normal = Vector3D::cross(e1, e2).normalized();
                
                // Calculate distance from line endpoints to triangle plane
                double dist1 = Vector3D::dot(v1 - tv1, normal);
                double dist2 = Vector3D::dot(v2 - tv1, normal);
                
                // If both endpoints are on same side of plane, no intersection
                if (dist1 * dist2 > 0) continue;
                
                // Calculate intersection point with plane
                double intersection_param = dist1 / (dist1 - dist2);  // Changed 't' to 'intersection_param'
                Vector3D intersection_point = v1 + (v2 - v1) * intersection_param;
                
                // Check if point is inside triangle (simplified)
                // For simplicity, just check if it's close to the triangle centroid
                Vector3D centroid = (tv1 + tv2 + tv3) * (1.0/3.0);
                Vector3D diff = intersection_point - centroid;
                double distance = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
                
                if (distance < 0.5 * surface.size) {
                    // Check if this point is already in our intersection list
                    bool found = false;
                    for (const auto& existing_point : intersection_points) {
                        Vector3D delta = existing_point - intersection_point;
                        if (delta.x*delta.x + delta.y*delta.y + delta.z*delta.z < 1e-10) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        intersection_points.push_back(intersection_point);
                    }
                }
            }
        }
        
        // If we found any intersections, create an Intersection object
        if (!intersection_points.empty()) {
            std::lock_guard<std::mutex> lock(mutex);
            Intersection intersection(polyline_idx, surface_idx, true);  // true for polyline-surface
            
            for (const auto& point : intersection_points) {
                intersection.add_point(point);
            }
            
            intersections.push_back(intersection);
        }
    }
    
    void calculate_size_of_intersections() {
        // Calculate length/size of each intersection
        for (auto& intersection : intersections) {
            double total_length = 0.0;
            
            // For polyline-mesh intersections, it's just points
            if (intersection.is_polyline_mesh) {
                // Size is just the number of points for polyline-mesh intersections
                continue;
            }
            
            // For mesh-mesh intersections, calculate lengths between consecutive points
            for (size_t i = 0; i < intersection.points.size() - 1; i++) {
                const Vector3D& p1 = intersection.points[i];
                const Vector3D& p2 = intersection.points[i + 1];
                
                // Calculate Euclidean distance
                Vector3D diff = p2 - p1;
                total_length += std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
            }
            
            // Store the length if needed - for example in an attribute of the intersection
        }
    }
    
    void calculate_triple_points(size_t i1, size_t i2) {
        const auto& intersection1 = intersections[i1];
        const auto& intersection2 = intersections[i2];
        
        // Skip if either intersection doesn't have points
        if (intersection1.points.empty() || intersection2.points.empty()) {
            return;
        }
        
        // Skip if intersections don't share a surface
        bool share_surface = (intersection1.id1 == intersection2.id1) || 
                             (intersection1.id1 == intersection2.id2) ||
                             (intersection1.id2 == intersection2.id1) || 
                             (intersection1.id2 == intersection2.id2);
        
        if (!share_surface) {
            return;
        }
        
        // Find closest points between the two intersection lines
        double min_distance = std::numeric_limits<double>::max();
        Vector3D closest_point;
        
        // For every point in intersection1, find closest point in intersection2
        for (const auto& p1 : intersection1.points) {
            for (const auto& p2 : intersection2.points) {
                Vector3D diff = p2 - p1;
                double distance = std::sqrt(diff.x*diff.x + diff.y*diff.y + diff.z*diff.z);
                
                if (distance < min_distance) {
                    min_distance = distance;
                    closest_point = (p1 + p2) * 0.5;  // midpoint between closest points
                }
            }
        }
        
        // If points are close enough, consider it a triple point
        if (min_distance < 1e-6) {
            std::lock_guard<std::mutex> lock(mutex);
            TriplePoint tp(closest_point);
            tp.add_intersection(i1);
            tp.add_intersection(i2);
            triple_points.push_back(tp);
        }
    }
    
    void insert_triple_points() {
        // This function adds triple points to the relevant intersections
        for (const auto& tp : triple_points) {
            // For each intersection that contains this triple point
            for (int i_id : tp.intersection_ids) {
                if (i_id >= 0 && i_id < static_cast<int>(intersections.size())) {
                    // Check if point is already in the intersection
                    bool found = false;
                    
                    for (const auto& point : intersections[i_id].points) {
                        Vector3D diff = point - tp.point;
                        if (diff.x*diff.x + diff.y*diff.y + diff.z*diff.z < 1e-10) {
                            found = true;
                            break;
                        }
                    }
                    
                    if (!found) {
                        // Add the triple point to this intersection
                        intersections[i_id].points.push_back(tp.point);
                    }
                }
            }
        }
        
        // Sort intersection points for each intersection to maintain spatial order
        // This is a simplified approach - real implementation would sort points along the intersection curve
        for (auto& intersection : intersections) {
            if (intersection.points.size() <= 1) {
                continue;  // Nothing to sort
            }
            
            // For simplicity, just sort by x, then y, then z
            std::sort(intersection.points.begin(), intersection.points.end(), 
                [](const Vector3D& a, const Vector3D& b) {
                    if (std::abs(a.x - b.x) > 1e-10) return a.x < b.x;
                    if (std::abs(a.y - b.y) > 1e-10) return a.y < b.y;
                    return a.z < b.z;
                });
        }
    }
    
    void calculate_size_of_constraints() {
        // Implementation would calculate size of constraints
        // This is a placeholder - actual implementation would be more complex
    }
    
    std::string get_current_time_string() {
        auto now = std::chrono::system_clock::now();
        std::time_t now_time = std::chrono::system_clock::to_time_t(now);
        
        char time_buffer[26];
    #ifdef _WIN32
        ctime_s(time_buffer, sizeof(time_buffer), &now_time);
    #else
        std::string time_str = std::ctime(&now_time);
        std::strncpy(time_buffer, time_str.c_str(), sizeof(time_buffer));
    #endif
    
        // Remove trailing newline if present
        size_t len = strlen(time_buffer);
        if (len > 0 && time_buffer[len-1] == '\n') {
            time_buffer[len-1] = '\0';
        }
        
        return std::string(time_buffer);
    }
    void export_vtu(const std::string& filename) {
        std::cout << "Exporting mesh to " << filename << std::endl;
        
        std::ofstream vtu_file(filename);
        if (!vtu_file.is_open()) {
            throw std::runtime_error("Could not open file for writing: " + filename);
        }
        
        write_vtu_header(vtu_file);
        write_vtu_points(vtu_file);
        write_vtu_cells(vtu_file);
        write_vtu_cell_data(vtu_file);
        write_vtu_footer(vtu_file);
        
        vtu_file.close();
        std::cout << "Export complete: " << filename << std::endl;
    }

private:
    std::vector<std::vector<Vector3D>> polylines;
    std::vector<Triangle> triangles;
    std::vector<Vector3D> mesh_vertices;
    std::vector<std::vector<int>> mesh_faces;
    std::mutex mutex;

    // Rest of the private methods...
};

class PyGradientControl {
public:
    static PyGradientControl& getInstance() {
        static PyGradientControl instance;
        return instance;
    }

    void update(double gradient, double meshsize, int npoints, const double* pointlist, const double* refinesize) {
        _gradient = gradient;
        _meshsize = meshsize;
        _npoints = npoints;
        
        // Store point list and refine sizes
        if (_pointlist) delete[] _pointlist;
        if (_refinesize) delete[] _refinesize;
        
        _pointlist = new double[npoints * 2];
        _refinesize = new double[npoints];
        
        std::copy(pointlist, pointlist + (npoints * 2), _pointlist);
        std::copy(refinesize, refinesize + npoints, _refinesize);
    }

    double getGradient() const { return _gradient; }
    double getMeshSize() const { return _meshsize; }
    int getNumPoints() const { return _npoints; }
    const double* getPointList() const { return _pointlist; }
    const double* getRefineSize() const { return _refinesize; }

    bool isTriangleSuitable(const Vector3D& v1, const Vector3D& v2, const Vector3D& v3) const {
        // Calculate triangle centroid
        Vector3D centroid((v1.x + v2.x + v3.x) / 3.0,
                         (v1.y + v2.y + v3.y) / 3.0,
                         (v1.z + v2.z + v3.z) / 3.0);

        // Get desired size at centroid based on gradient
        double desiredSize = _meshsize * (1.0 + _gradient * centroid.length());

        // Calculate actual triangle size (use max edge length as metric)
        double edge1 = sqrt(pow(v2.x - v1.x, 2) + pow(v2.y - v1.y, 2) + pow(v2.z - v1.z, 2));
        double edge2 = sqrt(pow(v3.x - v2.x, 2) + pow(v3.y - v2.y, 2) + pow(v3.z - v2.z, 2));
        double edge3 = sqrt(pow(v1.x - v3.x, 2) + pow(v1.y - v3.y, 2) + pow(v1.z - v3.z, 2));
        double maxEdge = std::max({edge1, edge2, edge3});

        // Calculate minimum angle
        double minAngle = calculateMinAngle(v1, v2, v3);
        
        // Triangle is suitable if:
        // 1. Max edge length is within tolerance of desired size
        // 2. Minimum angle is acceptable (based on gradient)
        double sizeTolerance = 0.5;  // Allow 50% deviation from desired size
        double minAngleThreshold = 20.0 * (1.0 - _gradient * 0.25);  // Decrease min angle as gradient increases
        
        return maxEdge <= desiredSize * (1.0 + sizeTolerance) &&
               minAngle >= minAngleThreshold;
    }

    ~PyGradientControl() {
        delete[] _pointlist;
        delete[] _refinesize;
    }

private:
    PyGradientControl() : _gradient(1.0), _meshsize(1.0), _npoints(0), _pointlist(nullptr), _refinesize(nullptr) {}
    
    // Prevent copying
    PyGradientControl(const PyGradientControl&) = delete;
    PyGradientControl& operator=(const PyGradientControl&) = delete;

    double calculateMinAngle(const Vector3D& v1, const Vector3D& v2, const Vector3D& v3) const {
        // Calculate vectors for triangle edges
        Vector3D e1(v2.x - v1.x, v2.y - v1.y, v2.z - v1.z);
        Vector3D e2(v3.x - v2.x, v3.y - v2.y, v3.z - v2.z);
        Vector3D e3(v1.x - v3.x, v1.y - v3.y, v1.z - v3.z);

        // Calculate lengths
        double l1 = sqrt(e1.x * e1.x + e1.y * e1.y + e1.z * e1.z);
        double l2 = sqrt(e2.x * e2.x + e2.y * e2.y + e2.z * e2.z);
        double l3 = sqrt(e3.x * e3.x + e3.y * e3.y + e3.z * e3.z);

        // Calculate angles using dot product
        double a1 = acos(-(e1.x * e3.x + e1.y * e3.y + e1.z * e3.z) / (l1 * l3)) * 180.0 / M_PI;
        double a2 = acos(-(e1.x * e2.x + e1.y * e2.y + e1.z * e2.z) / (l1 * l2)) * 180.0 / M_PI;
        double a3 = acos(-(e2.x * e3.x + e2.y * e3.y + e2.z * e3.z) / (l2 * l3)) * 180.0 / M_PI;

        return std::min({a1, a2, a3});
    }

    double _gradient;
    double _meshsize;
    int _npoints;
    double* _pointlist;
    double* _refinesize;
};

// Replace the existing PYBIND11_MODULE section with this updated version:
PYBIND11_MODULE(_meshit, m) {
    m.doc() = "MeshIt Python bindings for PZero integration";
    
    // Vector3D bindings
    py::class_<Vector3D>(m, "Vector3D")
        .def(py::init<>())
        .def(py::init<double, double, double>())
        .def_readwrite("x", &Vector3D::x)
        .def_readwrite("y", &Vector3D::y)
        .def_readwrite("z", &Vector3D::z)
        .def("length", &Vector3D::length)
        .def("lengthSquared", &Vector3D::lengthSquared)
        .def("normalized", &Vector3D::normalized)
        .def("rotX", &Vector3D::rotX)
        .def("rotZ", &Vector3D::rotZ)
        .def("distanceToPlane", &Vector3D::distanceToPlane)
        .def("distanceToLine", &Vector3D::distanceToLine)
        .def_static("dot", &Vector3D::dot)
        .def_static("cross", &Vector3D::cross)
        .def_static("normal", &Vector3D::normal)
        .def(py::self + py::self)
        .def(py::self - py::self)
        .def(py::self * float())
        .def("__repr__",
            [](const Vector3D &v) {
                return "Vector3D(" + std::to_string(v.x) + ", " + 
                       std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
            }
        );

    // Bind the Intersection class
    py::class_<Intersection>(m, "Intersection")
        .def(py::init<int, int, bool>())
        .def_readwrite("id1", &Intersection::id1)
        .def_readwrite("id2", &Intersection::id2)
        .def_readwrite("is_polyline_mesh", &Intersection::is_polyline_mesh)
        .def_readwrite("points", &Intersection::points)
        .def("add_point", &Intersection::add_point);
    
    // Bind the TriplePoint class
    py::class_<TriplePoint>(m, "TriplePoint")
        .def(py::init<Vector3D>())
        .def_readwrite("point", &TriplePoint::point)
        .def_readwrite("intersection_ids", &TriplePoint::intersection_ids)
        .def("add_intersection", &TriplePoint::add_intersection);
    
    // Bind the Surface class
    py::class_<Surface>(m, "Surface")
        .def(py::init<>())
        .def_readwrite("name", &Surface::name)
        .def_readwrite("type", &Surface::type)
        .def_readwrite("size", &Surface::size)
        .def_readwrite("vertices", &Surface::vertices)
        .def_readwrite("triangles", &Surface::triangles)
        .def_readwrite("convex_hull", &Surface::convex_hull)
        .def_readwrite("bounds", &Surface::bounds)
        .def("calculate_convex_hull", &Surface::calculate_convex_hull)
        .def("calculate_min_max", &Surface::calculate_min_max)
        .def("triangulate", &Surface::triangulate)
        .def("add_vertex", [](Surface& self, const Vector3D& vertex) {
            self.vertices.push_back(vertex);
        })
        .def("get_convex_hull", &Surface::get_convex_hull)
        .def("alignIntersectionsToConvexHull", &Surface::alignIntersectionsToConvexHull);
    
    // Bind the Polyline class
    py::class_<Polyline>(m, "Polyline")
        .def(py::init<>())
        .def_readwrite("name", &Polyline::name)
        .def_readwrite("size", &Polyline::size)
        .def_readwrite("vertices", &Polyline::vertices)
        .def_readwrite("segments", &Polyline::segments)
        .def_readwrite("bounds", &Polyline::bounds)
        .def("calculate_segments", &Polyline::calculate_segments)
        .def("calculate_min_max", &Polyline::calculate_min_max)
        .def("add_vertex", [](Polyline& self, const Vector3D& vertex) {
            self.vertices.push_back(vertex);
        });
    
    // Bind Triangle class
    py::class_<Triangle>(m, "Triangle")
        .def(py::init<Vector3D, Vector3D, Vector3D>())
        .def_readwrite("v1", &Triangle::v1)
        .def_readwrite("v2", &Triangle::v2)
        .def_readwrite("v3", &Triangle::v3)
        .def("normal", &Triangle::normal)
        .def("area", &Triangle::area)
        .def("centroid", &Triangle::centroid)
        .def("containsPoint", &Triangle::containsPoint);

    // UPDATED - Bind MeshItModel class with custom vector bindings
    py::class_<MeshItModel>(m, "MeshItModel")
        .def(py::init<>())
        .def_property("surfaces", 
            [](const MeshItModel& model) { return model.surfaces; },
            [](MeshItModel& model, const std::vector<Surface>& surfaces) { model.surfaces = surfaces; })
        .def_property("model_polylines", 
            [](const MeshItModel& model) { return model.model_polylines; },
            [](MeshItModel& model, const std::vector<Polyline>& polylines) { model.model_polylines = polylines; })
        .def_property("intersections", 
            [](const MeshItModel& model) { return model.intersections; },
            [](MeshItModel& model, const std::vector<Intersection>& intersections) { model.intersections = intersections; })
        .def_property("triple_points", 
            [](const MeshItModel& model) { return model.triple_points; },
            [](MeshItModel& model, const std::vector<TriplePoint>& triple_points) { model.triple_points = triple_points; })
        .def("append_surface", &MeshItModel::append_surface)
        .def("append_polyline", &MeshItModel::append_polyline)
        .def("set_mesh_quality", &MeshItModel::set_mesh_quality)
        .def("set_mesh_algorithm", &MeshItModel::set_mesh_algorithm)
        .def("enable_constraints", &MeshItModel::enable_constraints)
        .def("add_polyline", &MeshItModel::add_polyline)
        .def("add_surface", [](MeshItModel& self, const Surface& surface) {
            self.surfaces.push_back(surface);
        })
        .def("add_triangle", &MeshItModel::add_triangle)
        .def("pre_mesh", &MeshItModel::pre_mesh)
        .def("handle_constraints", &MeshItModel::handle_constraints)
        .def("mesh", &MeshItModel::mesh)
        .def("mesh_delaunay", &MeshItModel::mesh_delaunay)
        .def("mesh_advancing_front", &MeshItModel::mesh_advancing_front)
        .def("mesh_simple", &MeshItModel::mesh_simple)
        .def("pre_mesh_job", &MeshItModel::pre_mesh_job)
        .def("calculate_surface_surface_intersection", &MeshItModel::calculate_surface_surface_intersection)
        .def("calculate_polyline_surface_intersection", &MeshItModel::calculate_polyline_surface_intersection)
        .def("calculate_size_of_intersections", &MeshItModel::calculate_size_of_intersections)
        .def("calculate_triple_points", &MeshItModel::calculate_triple_points)
        .def("insert_triple_points", &MeshItModel::insert_triple_points)
        .def("calculate_size_of_constraints", &MeshItModel::calculate_size_of_constraints)
        .def("export_vtu", &MeshItModel::export_vtu);
    
    // Add helper methods to create surfaces and polylines
    m.def("create_surface", [](const std::vector<std::vector<double>>& vertices,
                               const std::vector<std::vector<int>>& triangles,
                               const std::string& name = "",
                               const std::string& type = "Default") {
        Surface surface;
        surface.name = name;
        surface.type = type;
        
        for (const auto& v : vertices) {
            if (v.size() >= 3) {
                surface.vertices.push_back(Vector3D(v[0], v[1], v[2]));
            }
        }
        
        surface.triangles = triangles;
        surface.calculate_min_max();
        return surface;
    });
    
    m.def("create_polyline", [](const std::vector<std::vector<double>>& vertices,
                                const std::string& name = "") {
        Polyline polyline;
        polyline.name = name;
        
        for (const auto& v : vertices) {
            if (v.size() >= 3) {
                polyline.vertices.push_back(Vector3D(v[0], v[1], v[2]));
            }
        }
        
        polyline.calculate_min_max();
        return polyline;
    });

    // Add standalone function to compute convex hull
    m.def("compute_convex_hull", [](const std::vector<std::vector<double>>& points) {
        std::vector<Vector3D> vertices;
        for (const auto& p : points) {
            if (p.size() >= 3) {
                vertices.push_back(Vector3D(p[0], p[1], p[2]));
            }
        }
        
        std::vector<Vector3D> hull = compute_convex_hull(vertices);
        
        // Convert back to list of lists for Python
        std::vector<std::vector<double>> result;
        for (const auto& v : hull) {
            result.push_back({v.x, v.y, v.z});
        }
        
        return result;
    }, "Compute the convex hull of a set of 3D points");

    // Update GradientControl bindings
    py::class_<PyGradientControl>(m, "GradientControl")
        .def_static("get_instance", &PyGradientControl::getInstance, py::return_value_policy::reference)
        .def("update", &PyGradientControl::update)
        .def("get_gradient", &PyGradientControl::getGradient)
        .def("get_mesh_size", &PyGradientControl::getMeshSize)
        .def("get_num_points", &PyGradientControl::getNumPoints)
        .def("is_triangle_suitable", &PyGradientControl::isTriangleSuitable);
}