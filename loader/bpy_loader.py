"""
Blender File Loader
Uses bpy (Blender Python API) to load .blend files.
Supports full Principled BSDF material extraction including advanced layers.
"""

import logging
from typing import List, Dict
from pathlib import Path

try:
    import bpy
    import bmesh
    HAS_BPY = True
except ImportError:
    HAS_BPY = False
    bpy = None  # Set to None for safe checking
    bmesh = None

from base_loader import BaseLoader, LoaderRegistry
from data_structures import (
    SceneData, Mesh, Material, Vertex,
    ClearcoatLayer, TransmissionLayer, SheenLayer,
    SubsurfaceLayer, AnisotropyLayer
)


logger = logging.getLogger(__name__)


# Only register if bpy is available
if HAS_BPY:
    @LoaderRegistry.register('.blend')
    class BlenderLoader(BaseLoader):
        """
        Loader for Blender .blend files.
        Requires Blender Python API (bpy) to be available.
        Supports full Principled BSDF material tree extraction.
        """
        
        def __init__(self, filepath: str):
            if not HAS_BPY:
                raise ImportError(
                    "bpy (Blender Python API) not available. "
                    "Install with: pip install bpy\n"
                    "Note: bpy package is large (~200MB) but provides full Blender functionality.\n"
                    f"Alternatively, run through system Blender:\n"
                    f"  blender --background --python main.py -- {filepath} output.json"
                )
            
            super().__init__(filepath)
        
        def supports_advanced_materials(self) -> bool:
            """Blender format supports full PBR material layers"""
            return True
        
        def load(self) -> SceneData:
            """Load Blender file using bpy"""
            logger.info(f"Loading Blender file: {self.filepath}")
            
            # Clear existing scene
            bpy.ops.wm.read_factory_settings(use_empty=True)
            
            # Load blend file
            bpy.ops.wm.open_mainfile(filepath=str(self.filepath))
            
            logger.info(f"Blend file loaded: {len(bpy.data.objects)} objects, "
                       f"{len(bpy.data.materials)} materials")
            
            # Extract materials
            materials, material_map = self._extract_materials()
            self.scene.materials = materials
            
            # Extract geometry
            meshes = self._extract_meshes(material_map)
            self.scene.meshes = meshes
            
            # Validate and log statistics
            self.validate_scene()
            self.log_statistics()
            
            return self.scene
        
        def _extract_materials(self) -> tuple[List[Material], Dict[str, int]]:
            """Extract materials from Blender scene"""
            materials = []
            material_map = {}
            
            for mat in bpy.data.materials:
                logger.debug(f"Processing material: {mat.name}")
                
                material = Material(name=mat.name)
                
                # Check if material uses nodes (Principled BSDF)
                if mat.use_nodes and mat.node_tree:
                    self._extract_principled_bsdf(mat.node_tree, material)
                else:
                    logger.warning(f"Material {mat.name} does not use nodes, using defaults")
                
                material_map[mat.name] = len(materials)
                materials.append(material)
            
            # Add default material if none exist
            if not materials:
                logger.warning("No materials found in Blender file, adding default")
                materials.append(Material(name="default"))
                material_map["default"] = 0
            
            return materials, material_map
        
        def _extract_principled_bsdf(self, node_tree, material: Material):
            """Extract properties from Principled BSDF node"""
            # Find Principled BSDF node
            principled_node = None
            for node in node_tree.nodes:
                if node.type == 'BSDF_PRINCIPLED':
                    principled_node = node
                    break
            
            if not principled_node:
                logger.warning(f"No Principled BSDF found in {material.name}")
                return
            
            inputs = principled_node.inputs
            
            # Base Color
            if 'Base Color' in inputs:
                base_color = inputs['Base Color'].default_value
                material.base_color = [
                    float(base_color[0]),
                    float(base_color[1]),
                    float(base_color[2])
                ]
        
            # Metallic
            if 'Metallic' in inputs:
                material.metallic = float(inputs['Metallic'].default_value)
        
            # Roughness
            if 'Roughness' in inputs:
                material.roughness = float(inputs['Roughness'].default_value)
        
            # Emission
            if 'Emission' in inputs:
                emission = inputs['Emission'].default_value
                if hasattr(emission, '__len__') and len(emission) >= 3:
                    material.emission = [
                        float(emission[0]),
                        float(emission[1]),
                        float(emission[2])
                    ]
        
            # IOR
            if 'IOR' in inputs:
                material.ior = float(inputs['IOR'].default_value)
            
            # Alpha (opacity)
            if 'Alpha' in inputs:
                material.opacity = float(inputs['Alpha'].default_value)
            
            logger.debug(f"  Base: color={material.base_color}, "
                        f"metallic={material.metallic:.2f}, "
                        f"roughness={material.roughness:.2f}")
            
            # Advanced layers
            self._extract_transmission(inputs, material)
            self._extract_clearcoat(inputs, material)
            self._extract_sheen(inputs, material)
            self._extract_subsurface(inputs, material)
            self._extract_anisotropy(inputs, material)
        
        def _extract_transmission(self, inputs: dict, material: Material):
            """Extract transmission (glass) layer"""
            if 'Transmission' not in inputs:
                return
            
            strength = float(inputs['Transmission'].default_value)
            if strength < 0.01:
                return
            
            # Transmission roughness (if available, otherwise use base roughness)
            roughness = material.roughness
            if 'Transmission Roughness' in inputs:
                roughness = float(inputs['Transmission Roughness'].default_value)
            
            material.transmission = TransmissionLayer(
                strength=strength,
                roughness=roughness,
                depth=0.0,  # Blender doesn't expose this directly
                color=material.base_color.copy(),
                texture_idx=-1
            )
            
            logger.debug(f"  Transmission: strength={strength:.2f}, roughness={roughness:.2f}")
        
        def _extract_clearcoat(self, inputs: dict, material: Material):
            """Extract clearcoat layer"""
            if 'Clearcoat' not in inputs:
                return
            
            strength = float(inputs['Clearcoat'].default_value)
            if strength < 0.01:
                return
            
            roughness = 0.0
            if 'Clearcoat Roughness' in inputs:
                roughness = float(inputs['Clearcoat Roughness'].default_value)
            
            material.clearcoat = ClearcoatLayer(
                strength=strength,
                roughness=roughness,
                ior=1.5,  # Standard clearcoat IOR
                texture_idx=-1
            )
            
            logger.debug(f"  Clearcoat: strength={strength:.2f}, roughness={roughness:.2f}")
        
        def _extract_sheen(self, inputs: dict, material: Material):
            """Extract sheen (fabric) layer"""
            if 'Sheen' not in inputs:
                return
            
            strength = float(inputs['Sheen'].default_value)
            if strength < 0.01:
                return
            
            # Sheen tint (color)
            color = [1.0, 1.0, 1.0]
            if 'Sheen Tint' in inputs:
                tint = inputs['Sheen Tint'].default_value
                if hasattr(tint, '__len__') and len(tint) >= 3:
                    color = [float(tint[0]), float(tint[1]), float(tint[2])]
            
            material.sheen = SheenLayer(
                strength=strength,
                roughness=0.0,  # Blender doesn't expose sheen roughness directly
                color=color,
                texture_idx=-1
            )
            
            logger.debug(f"  Sheen: strength={strength:.2f}, color={color}")
        
        def _extract_subsurface(self, inputs: dict, material: Material):
            """Extract subsurface scattering layer"""
            if 'Subsurface' not in inputs and 'Subsurface Weight' not in inputs:
                return
            
            # Try 'Subsurface Weight' first (newer Blender versions)
            strength = 0.0
            if 'Subsurface Weight' in inputs:
                strength = float(inputs['Subsurface Weight'].default_value)
            elif 'Subsurface' in inputs:
                strength = float(inputs['Subsurface'].default_value)
            
            if strength < 0.01:
                return
            
            # Subsurface radius (scattering distance)
            radius = [1.0, 1.0, 1.0]
            if 'Subsurface Radius' in inputs:
                rad = inputs['Subsurface Radius'].default_value
                if hasattr(rad, '__len__') and len(rad) >= 3:
                    radius = [float(rad[0]), float(rad[1]), float(rad[2])]
            
            # Subsurface color
            color = material.base_color.copy()
            if 'Subsurface Color' in inputs:
                sss_color = inputs['Subsurface Color'].default_value
                if hasattr(sss_color, '__len__') and len(sss_color) >= 3:
                    color = [float(sss_color[0]), float(sss_color[1]), float(sss_color[2])]
            
            material.subsurface = SubsurfaceLayer(
                strength=strength,
                radius=radius,
                color=color,
                texture_idx=-1
            )
            
            logger.debug(f"  Subsurface: strength={strength:.2f}, radius={radius}")
        
        def _extract_anisotropy(self, inputs: dict, material: Material):
            """Extract anisotropic reflection layer"""
            if 'Anisotropic' not in inputs:
                return
            
            strength = float(inputs['Anisotropic'].default_value)
            if strength < 0.01:
                return
            
            rotation = 0.0
            if 'Anisotropic Rotation' in inputs:
                rotation = float(inputs['Anisotropic Rotation'].default_value)
            
            material.anisotropy = AnisotropyLayer(
                strength=strength,
                rotation=rotation,
                texture_idx=-1
            )
            
            logger.debug(f"  Anisotropy: strength={strength:.2f}, rotation={rotation:.2f}")
        
        def _extract_meshes(self, material_map: Dict[str, int]) -> List[Mesh]:
            """Extract geometry from Blender objects"""
            meshes = []
            
            for obj in bpy.data.objects:
                if obj.type != 'MESH':
                    continue
                
                mesh_data = obj.data
                logger.debug(f"Processing mesh: {obj.name} "
                            f"({len(mesh_data.vertices)} verts, "
                            f"{len(mesh_data.polygons)} faces)")
                
                # Get material index
                mat_idx = 0
                if obj.material_slots and obj.material_slots[0].material:
                    mat_name = obj.material_slots[0].material.name
                    mat_idx = material_map.get(mat_name, 0)
                
                # Ensure mesh has UV and normals
                if not mesh_data.uv_layers:
                    mesh_data.uv_layers.new(name="UVMap")
                mesh_data.calc_normals_split()
                
                # Triangulate mesh
                bm = bmesh.new()
                bm.from_mesh(mesh_data)
                bmesh.ops.triangulate(bm, faces=bm.faces)
                bm.to_mesh(mesh_data)
                bm.free()
                
                # Extract vertices and indices
                vertices, indices = self._extract_vertex_data(mesh_data)
                
                mesh = Mesh(
                    name=obj.name,
                    vertices=vertices,
                    indices=indices,
                    material_index=mat_idx
                )
                
                meshes.append(mesh)
                logger.debug(f"  Extracted: {len(vertices)} vertices, "
                            f"{len(indices) // 3} triangles, "
                            f"material {mat_idx}")
            
            return meshes
        
        def _extract_vertex_data(self, mesh_data) -> tuple[List[Vertex], List[int]]:
            """Extract vertex and index data from Blender mesh"""
            vertices = []
            indices = []
            uv_layer = mesh_data.uv_layers.active.data if mesh_data.uv_layers else None
            
            # Process each polygon (triangle after triangulation)
            for poly in mesh_data.polygons:
                for loop_idx in poly.loop_indices:
                    loop = mesh_data.loops[loop_idx]
                    vert = mesh_data.vertices[loop.vertex_index]
                    
                    # Position
                    position = [
                        float(vert.co.x),
                        float(vert.co.y),
                        float(vert.co.z)
                    ]
                    
                    # Normal
                    normal = [
                        float(loop.normal.x),
                        float(loop.normal.y),
                        float(loop.normal.z)
                    ]
                    
                    # UV
                    if uv_layer:
                        uv = uv_layer[loop_idx].uv
                        texcoord = [float(uv[0]), float(uv[1])]
                    else:
                        texcoord = [0.0, 0.0]
                    
                    # Tangent (compute from UV derivatives, or use default)
                    # TODO: Calculate proper tangent from UV mapping
                    tangent = [1.0, 0.0, 0.0]
                    
                    vertex = Vertex(
                        position=position,
                        normal=normal,
                        texcoord=texcoord,
                        tangent=tangent
                    )
                    
                    indices.append(len(vertices))
                    vertices.append(vertex)
            
            return vertices, indices
