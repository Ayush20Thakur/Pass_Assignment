# Data Provenance and Specifications

This document defines the origin, mathematical formulation, license, coordinate units, and schema for all test and example data bundled with the Forge workbench.

---

## 1. Geometry: `cube_or_bracket.stl`

- **Type**: ASCII Stereolithography (STL) surface mesh.
- **Source**: Self-created procedural CAD geometry representing a standard mounting L-bracket.
- **License**: CC0 1.0 Universal (Public Domain Dedication).
- **Physical Units**: Metres (m).
- **Dimensions**:
  - Outer envelope: $0.100\ \text{m} \times 0.100\ \text{m} \times 0.020\ \text{m}$ ($100\ \text{mm} \times 100\ \text{mm} \times 20\ \text{mm}$).
  - Arm thickness: $0.020\ \text{m}$ ($20\ \text{mm}$).
- **Topology**:
  - Closed, orientable, 2-manifold surface.
  - Triangle count: 20 facets.
  - Degenerate triangles: 0.
- **Validation**:
  - Conforms to standard ASCII STL format with outer loops and unit-normalized face normals.

---

## 2. Nodal Temperature Field: `nodal_temperature_synthetic.csv`

> [!IMPORTANT]
> **Synthetic Notice**: This CSV file is **strictly synthetic**. It was generated numerically from an analytical closed-form 3D temperature equation. In strict compliance with assignment Rule 4, it is explicitly labelled as synthetic and is **never** represented as an export from commercial ANSYS software.

- **Type**: Synthetic Nodal Temperature CSV Dataset.
- **Source**: Procedural generation script using analytical 3D thermal field equation:
  $$T(x, y, z) = 100.0 - 60.0 \left(\frac{x}{0.1}\right) + 25.0 \sin\left(\frac{\pi y}{0.1}\right) \cos\left(\frac{\pi z}{0.02}\right)$$
- **License**: CC0 1.0 Universal (Public Domain Dedication).
- **Physical Units**:
  - Spatial coordinates ($x, y, z$): Metres ($\text{m}$).
  - Temperature: Degrees Celsius ($^\circ\text{C}$).
- **Schema**:
  1. `node_id`: Non-negative unique integer node index (1-based).
  2. `x_m`: X coordinate in metres (IEEE 754 double).
  3. `y_m`: Y coordinate in metres (IEEE 754 double).
  4. `z_m`: Z coordinate in metres (IEEE 754 double).
  5. `temperature_C`: Local temperature in degrees Celsius.
- **Dataset Properties**:
  - Node count: 925 unique nodes.
  - Minimum Temperature ($T_{min}$): $15.00\ ^\circ\text{C}$.
  - Maximum Temperature ($T_{max}$): $119.88\ ^\circ\text{C}$.
  - Bounding Box: $x \in [0.0, 0.1]\ \text{m}$, $y \in [0.0, 0.1]\ \text{m}$, $z \in [0.0, 0.02]\ \text{m}$.
