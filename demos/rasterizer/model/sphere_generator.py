#!/usr/bin/env python3
"""
Sphere Generator - Generates triangulated sphere geometry in custom text format.

Usage:
    python sphere_generator.py -o output.txt [options]

Options:
    -o, --output    Output file path (required)
    -c, --center    Center position as x,y,z (default: 0,0,0)
    -r, --radius    Sphere radius (default: 1.0)
    --rings         Latitude segments (default: 32)
    --sectors       Longitude segments (default: 32)
    --name          Object name comment (default: sphere)

Example:
    python sphere_generator.py -o sphere.txt -c 1.5,-1.5,0 -r 1.25 --name my_sphere
"""

import argparse
import math
import sys


def parse_vec3(s: str) -> tuple[float, float, float]:
    """Parse a comma-separated vector string like '1.5,-1.5,0' into a tuple."""
    parts = s.split(",")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError(f"Invalid vector format: '{s}'. Expected x,y,z")
    try:
        return tuple(float(p) for p in parts)
    except ValueError:
        raise argparse.ArgumentTypeError(f"Invalid vector format: '{s}'. Expected numeric values")


def generate_triangulated_sphere(
    file_path: str,
    center: tuple[float, float, float] = (0, 0, 0),
    radius: float = 1.0,
    rings: int = 32,
    sectors: int = 32,
    name: str = "sphere",
):
    """
    Generate a triangulated sphere in custom text format.

    Arguments:
        file_path: Output file path
        center: Center of the sphere (x, y, z)
        radius: Radius of the sphere
        rings: Number of latitude segments (north pole to south pole)
        sectors: Number of longitude segments (around the sphere)
        name: Object name for the comment
    """
    cx, cy, cz = center
    vertices = []
    normals = []
    faces = []

    # Generate vertices and normals
    # From north pole (stack_angle=0) to south pole (stack_angle=pi)
    for r in range(rings + 1):
        stack_angle = math.pi * r / rings  # 0 to pi
        sin_stack = math.sin(stack_angle)
        cos_stack = math.cos(stack_angle)

        for s in range(sectors + 1):
            sector_angle = 2 * math.pi * s / sectors  # 0 to 2*pi

            # Calculate normal direction (pointing outward from the center)
            nx = sin_stack * math.cos(sector_angle)
            ny = cos_stack  # North pole is up (y+)
            nz = sin_stack * math.sin(sector_angle)

            # Calculate vertex position
            x = cx + radius * nx
            y = cy + radius * ny
            z = cz + radius * nz

            vertices.append((x, y, z))
            normals.append((nx, ny, nz))

    # Generate faces (triangles)
    # Use counter-clockwise order (as seen from outside) so that normals point outward
    for r in range(rings):
        for s in range(sectors):
            # Indices of the four vertices of the current quad (1-based for OBJ format)
            # p1 --- p2
            # |  \   |
            # |   \  |
            # p4 --- p3
            p1 = r * (sectors + 1) + s
            p2 = p1 + 1
            p4 = (r + 1) * (sectors + 1) + s
            p3 = p4 + 1

            # Upper triangle (p1, p2, p3) - skip degenerate triangles at the north pole
            if r != 0:
                faces.append(f"{p1}/ {p2}/ {p3}/")

            # Lower triangle (p1, p3, p4) - skip degenerate triangles at the south pole
            if r != rings - 1:
                faces.append(f"{p1}/ {p3}/ {p4}/")

    # Write to file
    with open(file_path, "w") as f:
        f.write(f"Object # {name}\n")

        for i in range(len(vertices)):
            v = vertices[i]
            n = normals[i]
            f.write(
                f"    Vertex {v[0]:.6f} {v[1]:.6f} {v[2]:.6f} / {n[0]:.6f} {n[1]:.6f} {n[2]:.6f}\n"
            )

        for face in faces:
            f.write(f"    Face {face}\n")

    print(f"Generated: {file_path}")
    print(f"  Center: ({cx}, {cy}, {cz})")
    print(f"  Radius: {radius}")
    print(f"  Rings: {rings}, Sectors: {sectors}")
    print(f"  Vertices: {len(vertices)}, Faces: {len(faces)}")


def main():
    parser = argparse.ArgumentParser(
        description="Generate triangulated sphere geometry",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s -o sphere.txt
  %(prog)s -o sphere.txt -c 1.5,-1.5,0 -r 1.25
  %(prog)s -o sphere.txt --rings 64 --sectors 64 --name hi_res_sphere
        """,
    )
    parser.add_argument("-o", "--output", required=True, help="Output file path")
    parser.add_argument(
        "-c",
        "--center",
        type=parse_vec3,
        default=(0, 0, 0),
        metavar="X,Y,Z",
        help="Center position (default: 0,0,0)",
    )
    parser.add_argument(
        "-r", "--radius", type=float, default=1.0, help="Sphere radius (default: 1.0)"
    )
    parser.add_argument(
        "--rings", type=int, default=32, help="Latitude segments (default: 32)"
    )
    parser.add_argument(
        "--sectors", type=int, default=32, help="Longitude segments (default: 32)"
    )
    parser.add_argument(
        "--name", default="sphere", help="Object name comment (default: sphere)"
    )

    args = parser.parse_args()

    if args.radius <= 0:
        print("Error: Radius must be positive", file=sys.stderr)
        sys.exit(1)
    if args.rings < 2 or args.sectors < 3:
        print("Error: Rings must be >= 2 and sectors must be >= 3", file=sys.stderr)
        sys.exit(1)

    generate_triangulated_sphere(
        file_path=args.output,
        center=args.center,
        radius=args.radius,
        rings=args.rings,
        sectors=args.sectors,
        name=args.name,
    )


if __name__ == "__main__":
    main()