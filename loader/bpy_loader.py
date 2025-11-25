"""
Blender File Loader
Uses bpy (Blender Python API) to load .blend files.
Supports full Principled BSDF material extraction including advanced layers.

References:
- Blender Python API: https://docs.blender.org/api/current/index.html
- bpy.types.ShaderNodeBsdfPrincipled: https://docs.blender.org/api/current/bpy.types.ShaderNodeBsdfPrincipled.html
- bpy.types.Mesh: https://docs.blender.org/api/current/bpy.types.Mesh.html
"""

import logging
from typing import List, Dict, Tuple, Optional
from pathlib import Path
import math

try:
    import bpy
    import bmesh
    from mathutils import Vector
    HAS_BPY = True
except ImportError:
    HAS_BPY = False
    bpy = None
    bmesh = None
    Vector = None

from base_loader import BaseLoader, LoaderRegistry
from data_structures import (
    SceneData, Mesh, Material, Vertex, Texture,
    ClearcoatLayer, TransmissionLayer, SheenLayer,
    SubsurfaceLayer, AnisotropyLayer, IridescenceLayer, VolumeLayer
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
            try:
                bpy.ops.wm.open_mainfile(filepath=str(self.filepath))
            except Exception as e:
                logger.error(f"Failed to open Blender file: {e}")
                raise
            
            logger.info(f"Blend file loaded: {len(bpy.data.objects)} objects, "
                       f"{len(bpy.data.materials)} materials, "
                       f"{len(bpy.data.images)} textures")
            
            # Extract textures first (so we can reference them in materials)
            textures, texture_map = self._extract_textures()
            self.scene.textures = textures
            
            # Extract materials with texture references
            materials, material_map = self._extract_materials(texture_map)
            self.scene.materials = materials
            
            # Extract geometry
            meshes = self._extract_meshes(material_map)
            self.scene.meshes = meshes
            
            # Validate and log statistics
            self.validate_scene()
            self.log_statistics()
            
            return self.scene
        
        def _extract_textures(self) -> Tuple[List[Texture], Dict[str, int]]:
            """Extract textures from Blender images"""
            textures = []
            texture_map = {}
            
            for img in bpy.data.images:
                if img.filepath:
                    # Convert Blender path to absolute path
                    texture_path = bpy.path.abspath(img.filepath)
                    texture = Texture(path=texture_path)
                    texture_map[img.name] = len(textures)
                    textures.append(texture)
                    logger.debug(f"Texture: {img.name} -> {texture_path}")
            
            return textures, texture_map
        
        def _extract_materials(self, texture_map: Dict[str, int]) -> Tuple[List[Material], Dict[str, int]]:
            """Extract materials from Blender scene"""
            materials = []
            material_map = {}
            
            for mat in bpy.data.materials:
                logger.debug(f"Processing material: {mat.name}")
                
                material = Material(name=mat.name)
                
                # Check if material uses nodes (Principled BSDF)
                if mat.use_nodes and mat.node_tree:
                    self._extract_principled_bsdf(mat.node_tree, material, texture_map)
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
        
        def _extract_principled_bsdf(self, node_tree, material: Material, texture_map: Dict[str, int]):
            """
            Extract properties from Principled BSDF node.
            Reference: https://docs.blender.org/api/current/bpy.types.ShaderNodeBsdfPrincipled.html
            """
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
            
            # Helper to get value and check for texture connection
            def get_value_and_texture(socket_name: str, default_value, is_color=False):
                """Get socket value and check for connected texture"""
                if socket_name not in inputs:
                    return default_value, -1
                
                socket = inputs[socket_name]
                texture_idx = -1
                
                # Check if texture is connected
                if socket.is_linked:
                    link = socket.links[0]
                    from_node = link.from_node
                    
                    # Check if it's an Image Texture node
                    if from_node.type == 'TEX_IMAGE' and from_node.image:
                        img_name = from_node.image.name
                        texture_idx = texture_map.get(img_name, -1)
                        logger.debug(f"  {socket_name} texture: {img_name} (idx={texture_idx})")
                
                # Get default value
                value = socket.default_value
                if is_color and hasattr(value, '__len__') and len(value) >= 3:
                    return [float(value[0]), float(value[1]), float(value[2])], texture_idx
                elif not is_color:
                    return float(value), texture_idx
                else:
                    return default_value, texture_idx
            
            # Base Color
            base_color, base_tex_idx = get_value_and_texture('Base Color', [0.8, 0.8, 0.8], is_color=True)
            material.base_color = base_color
            material.base_color_texture = base_tex_idx
            
            # Metallic
            metallic, _ = get_value_and_texture('Metallic', 0.0)
            material.metallic = metallic
            
            # Roughness
            roughness, roughness_tex_idx = get_value_and_texture('Roughness', 0.5)
            material.roughness = roughness
            material.metallic_roughness_texture = roughness_tex_idx
            
            # Emission
            emission_strength = 0.0
            if 'Emission Strength' in inputs:
                emission_strength, _ = get_value_and_texture('Emission Strength', 0.0)
            
            emission_color = [0.0, 0.0, 0.0]
            emission_tex_idx = -1
            if 'Emission' in inputs or 'Emission Color' in inputs:
                socket_name = 'Emission Color' if 'Emission Color' in inputs else 'Emission'
                emission_color, emission_tex_idx = get_value_and_texture(socket_name, [0.0, 0.0, 0.0], is_color=True)
            
            # Combine emission color and strength
            material.emission = [c * emission_strength for c in emission_color]
            material.emission_texture = emission_tex_idx
            
            # IOR
            ior, _ = get_value_and_texture('IOR', 1.5)
            material.ior = ior
            
            # Alpha (opacity)
            alpha, _ = get_value_and_texture('Alpha', 1.0)
            material.opacity = alpha
            
            # Normal map
            if 'Normal' in inputs and inputs['Normal'].is_linked:
                link = inputs['Normal'].links[0]
                from_node = link.from_node
                if from_node.type == 'NORMAL_MAP':
                    # Check if normal map has texture input
                    if 'Color' in from_node.inputs and from_node.inputs['Color'].is_linked:
                        tex_link = from_node.inputs['Color'].links[0]
                        tex_node = tex_link.from_node
                        if tex_node.type == 'TEX_IMAGE' and tex_node.image:
                            img_name = tex_node.image.name
                            material.normal_texture = texture_map.get(img_name, -1)
                            logger.debug(f"  Normal texture: {img_name}")
            
            logger.debug(f"  Base: color={material.base_color}, "
                        f"metallic={material.metallic:.2f}, "
                        f"roughness={material.roughness:.2f}, "
                        f"ior={material.ior:.2f}, "
                        f"alpha={material.opacity:.2f}")
            
            # Advanced layers
            self._extract_transmission(inputs, material)
            self._extract_clearcoat(inputs, material)
            self._extract_sheen(inputs, material)
            self._extract_subsurface(inputs, material)
            self._extract_anisotropy(inputs, material)
            
            # Check for volume shader in material output
            self._extract_volume_shader(node_tree, material)
        
        def _extract_transmission(self, inputs: dict, material: Material):
            """Extract transmission (glass) layer"""
            if 'Transmission' not in inputs and 'Transmission Weight' not in inputs:
                return
            
            # Try 'Transmission Weight' first (newer Blender versions)
            strength = 0.0
            if 'Transmission Weight' in inputs:
                strength = float(inputs['Transmission Weight'].default_value)
            elif 'Transmission' in inputs:
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
                texture_index=-1,
                color=material.base_color.copy(),
                padding=0.0
            )
            
            logger.debug(f"  Transmission: strength={strength:.2f}, roughness={roughness:.2f}")
        
        def _extract_clearcoat(self, inputs: dict, material: Material):
            """Extract clearcoat layer"""
            if 'Clearcoat' not in inputs and 'Coat Weight' not in inputs:
                return
            
            # Try 'Coat Weight' first (Blender 4.0+)
            strength = 0.0
            if 'Coat Weight' in inputs:
                strength = float(inputs['Coat Weight'].default_value)
            elif 'Clearcoat' in inputs:
                strength = float(inputs['Clearcoat'].default_value)
            
            if strength < 0.01:
                return
            
            roughness = 0.0
            if 'Coat Roughness' in inputs:
                roughness = float(inputs['Coat Roughness'].default_value)
            elif 'Clearcoat Roughness' in inputs:
                roughness = float(inputs['Clearcoat Roughness'].default_value)
            
            ior = 1.5
            if 'Coat IOR' in inputs:
                ior = float(inputs['Coat IOR'].default_value)
            
            material.clearcoat = ClearcoatLayer(
                strength=strength,
                roughness=roughness,
                ior=ior,
                texture_index=-1,
                color=[1.0, 1.0, 1.0],
                padding=0.0
            )
            
            logger.debug(f"  Clearcoat: strength={strength:.2f}, roughness={roughness:.2f}, ior={ior:.2f}")
        
        def _extract_sheen(self, inputs: dict, material: Material):
            """Extract sheen (fabric) layer"""
            if 'Sheen' not in inputs and 'Sheen Weight' not in inputs:
                return
            
            # Try 'Sheen Weight' first (Blender 4.0+)
            strength = 0.0
            if 'Sheen Weight' in inputs:
                strength = float(inputs['Sheen Weight'].default_value)
            elif 'Sheen' in inputs:
                strength = float(inputs['Sheen'].default_value)
            
            if strength < 0.01:
                return
            
            # Sheen tint/roughness
            roughness = 0.5
            if 'Sheen Roughness' in inputs:
                roughness = float(inputs['Sheen Roughness'].default_value)
            
            # Sheen tint (color)
            color = [1.0, 1.0, 1.0]
            if 'Sheen Tint' in inputs:
                tint = inputs['Sheen Tint'].default_value
                if hasattr(tint, '__len__') and len(tint) >= 3:
                    color = [float(tint[0]), float(tint[1]), float(tint[2])]
            elif 'Sheen Color' in inputs:
                sheen_col = inputs['Sheen Color'].default_value
                if hasattr(sheen_col, '__len__') and len(sheen_col) >= 3:
                    color = [float(sheen_col[0]), float(sheen_col[1]), float(sheen_col[2])]
            
            material.sheen = SheenLayer(
                strength=strength,
                roughness=roughness,
                tint=0.0,  # Legacy field, using color instead
                texture_index=-1,
                color=color,
                padding=0.0
            )
            
            logger.debug(f"  Sheen: strength={strength:.2f}, roughness={roughness:.2f}, color={color}")
        
        def _extract_subsurface(self, inputs: dict, material: Material):
            """Extract subsurface scattering layer"""
            if 'Subsurface' not in inputs and 'Subsurface Weight' not in inputs:
                return
            
            # Try 'Subsurface Weight' first (Blender 4.0+)
            strength = 0.0
            if 'Subsurface Weight' in inputs:
                strength = float(inputs['Subsurface Weight'].default_value)
            elif 'Subsurface' in inputs:
                strength = float(inputs['Subsurface'].default_value)
            
            if strength < 0.01:
                return
            
            # Subsurface scale/radius
            scale = 1.0
            if 'Subsurface Scale' in inputs:
                scale = float(inputs['Subsurface Scale'].default_value)
            
            # Subsurface radius (per-channel scattering distance)
            radius = 1.0
            if 'Subsurface Radius' in inputs:
                rad = inputs['Subsurface Radius'].default_value
                if hasattr(rad, '__len__') and len(rad) >= 3:
                    # Use average of RGB channels
                    radius = (float(rad[0]) + float(rad[1]) + float(rad[2])) / 3.0
            
            # Subsurface color
            color = material.base_color.copy()
            if 'Subsurface Color' in inputs:
                sss_color = inputs['Subsurface Color'].default_value
                if hasattr(sss_color, '__len__') and len(sss_color) >= 3:
                    color = [float(sss_color[0]), float(sss_color[1]), float(sss_color[2])]
            
            material.subsurface = SubsurfaceLayer(
                strength=strength,
                radius=radius,
                scale=scale,
                texture_index=-1,
                color=color,
                padding=0.0
            )
            
            logger.debug(f"  Subsurface: strength={strength:.2f}, radius={radius:.2f}, scale={scale:.2f}")
        
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
                padding0=0.0,
                texture_index=-1,
                tangent=[1.0, 0.0, 0.0],
                padding1=0.0
            )
            
            logger.debug(f"  Anisotropy: strength={strength:.2f}, rotation={rotation:.2f}")
        
        def _extract_volume_shader(self, node_tree, material: Material):
            """
            Extract volume scattering and absorption from Principled Volume or Volume Scatter/Absorption nodes.
            Reference: https://docs.blender.org/api/current/bpy.types.ShaderNodeVolumePrincipled.html
                       https://docs.blender.org/api/current/bpy.types.ShaderNodeVolumeScatter.html
                       https://docs.blender.org/api/current/bpy.types.ShaderNodeVolumeAbsorption.html
            """
            # Find Material Output node
            output_node = None
            for node in node_tree.nodes:
                if node.type == 'OUTPUT_MATERIAL':
                    output_node = node
                    break
            
            if not output_node or 'Volume' not in output_node.inputs:
                return
            
            volume_socket = output_node.inputs['Volume']
            if not volume_socket.is_linked:
                return
            
            # Get the connected volume node
            volume_node = volume_socket.links[0].from_node
            
            # Try Principled Volume first (Blender 2.8+)
            if volume_node.type == 'VOLUME_PRINCIPLED':
                self._extract_principled_volume(volume_node, material)
            # Try Volume Scatter + Volume Absorption combination
            elif volume_node.type == 'VOLUME_SCATTER':
                self._extract_volume_scatter(volume_node, material, node_tree)
            elif volume_node.type == 'VOLUME_ABSORPTION':
                self._extract_volume_absorption(volume_node, material, node_tree)
            # Try Mix Shader combining scatter and absorption
            elif volume_node.type in ['MIX_SHADER', 'ADD_SHADER']:
                self._extract_mixed_volume(volume_node, material, node_tree)
        
        def _extract_principled_volume(self, volume_node, material: Material):
            """Extract from Principled Volume node"""
            inputs = volume_node.inputs
            
            # Density
            density = 1.0
            if 'Density' in inputs:
                density = float(inputs['Density'].default_value)
            
            if density < 0.001:
                return
            
            # Scattering color and distance
            scatter_color = [1.0, 1.0, 1.0]
            if 'Color' in inputs:
                color = inputs['Color'].default_value
                if hasattr(color, '__len__') and len(color) >= 3:
                    scatter_color = [float(color[0]), float(color[1]), float(color[2])]
            
            scatter_distance = 1.0
            # Anisotropy affects scattering behavior
            anisotropy = 0.0
            if 'Anisotropy' in inputs:
                anisotropy = float(inputs['Anisotropy'].default_value)
            
            # Absorption color (complementary to scattering)
            absorption_color = [0.0, 0.0, 0.0]
            if 'Absorption Color' in inputs:
                abs_color = inputs['Absorption Color'].default_value
                if hasattr(abs_color, '__len__') and len(abs_color) >= 3:
                    absorption_color = [float(abs_color[0]), float(abs_color[1]), float(abs_color[2])]
            
            # Emission (volumetric emission)
            emission_strength = 0.0
            if 'Emission Strength' in inputs:
                emission_strength = float(inputs['Emission Strength'].default_value)
            
            if emission_strength > 0.0 and 'Emission Color' in inputs:
                emission_color = inputs['Emission Color'].default_value
                if hasattr(emission_color, '__len__') and len(emission_color) >= 3:
                    # Add emission to scatter color
                    scatter_color = [
                        scatter_color[0] + float(emission_color[0]) * emission_strength,
                        scatter_color[1] + float(emission_color[1]) * emission_strength,
                        scatter_color[2] + float(emission_color[2]) * emission_strength
                    ]
            
            material.volume = VolumeLayer(
                scatter_color=scatter_color,
                scatter_distance=scatter_distance,
                absorption_color=absorption_color,
                density=density
            )
            
            logger.debug(f"  Volume (Principled): density={density:.2f}, "
                        f"scatter={scatter_color}, absorption={absorption_color}")
        
        def _extract_volume_scatter(self, scatter_node, material: Material, node_tree):
            """Extract from Volume Scatter node"""
            inputs = scatter_node.inputs
            
            # Scattering color
            scatter_color = [1.0, 1.0, 1.0]
            if 'Color' in inputs:
                color = inputs['Color'].default_value
                if hasattr(color, '__len__') and len(color) >= 3:
                    scatter_color = [float(color[0]), float(color[1]), float(color[2])]
            
            # Density
            density = 1.0
            if 'Density' in inputs:
                density = float(inputs['Density'].default_value)
            
            if density < 0.001:
                return
            
            # Anisotropy
            anisotropy = 0.0
            if 'Anisotropy' in inputs:
                anisotropy = float(inputs['Anisotropy'].default_value)
            
            # Try to find accompanying Volume Absorption
            absorption_color = [0.0, 0.0, 0.0]
            for node in node_tree.nodes:
                if node.type == 'VOLUME_ABSORPTION':
                    if 'Color' in node.inputs:
                        abs_col = node.inputs['Color'].default_value
                        if hasattr(abs_col, '__len__') and len(abs_col) >= 3:
                            absorption_color = [float(abs_col[0]), float(abs_col[1]), float(abs_col[2])]
                    break
            
            material.volume = VolumeLayer(
                scatter_color=scatter_color,
                scatter_distance=1.0,
                absorption_color=absorption_color,
                density=density
            )
            
            logger.debug(f"  Volume (Scatter): density={density:.2f}, color={scatter_color}")
        
        def _extract_volume_absorption(self, absorption_node, material: Material, node_tree):
            """Extract from Volume Absorption node"""
            inputs = absorption_node.inputs
            
            # Absorption color
            absorption_color = [0.0, 0.0, 0.0]
            if 'Color' in inputs:
                color = inputs['Color'].default_value
                if hasattr(color, '__len__') and len(color) >= 3:
                    absorption_color = [float(color[0]), float(color[1]), float(color[2])]
            
            # Density
            density = 1.0
            if 'Density' in inputs:
                density = float(inputs['Density'].default_value)
            
            if density < 0.001:
                return
            
            material.volume = VolumeLayer(
                scatter_color=[1.0, 1.0, 1.0],  # No scattering, pure absorption
                scatter_distance=1.0,
                absorption_color=absorption_color,
                density=density
            )
            
            logger.debug(f"  Volume (Absorption): density={density:.2f}, color={absorption_color}")
        
        def _extract_mixed_volume(self, mix_node, material: Material, node_tree):
            """Extract from Mix/Add Shader combining volume nodes"""
            # This is a simplified extraction for mixed volume shaders
            # In practice, this would need more sophisticated handling
            
            scatter_color = [1.0, 1.0, 1.0]
            absorption_color = [0.0, 0.0, 0.0]
            density = 1.0
            
            # Check both shader inputs
            for i in [1, 2]:  # Shader inputs are typically named 1 and 2
                input_name = str(i)
                if input_name in mix_node.inputs and mix_node.inputs[input_name].is_linked:
                    linked_node = mix_node.inputs[input_name].links[0].from_node
                    
                    if linked_node.type == 'VOLUME_SCATTER':
                        if 'Color' in linked_node.inputs:
                            color = linked_node.inputs['Color'].default_value
                            if hasattr(color, '__len__') and len(color) >= 3:
                                scatter_color = [float(color[0]), float(color[1]), float(color[2])]
                        if 'Density' in linked_node.inputs:
                            density = max(density, float(linked_node.inputs['Density'].default_value))
                    
                    elif linked_node.type == 'VOLUME_ABSORPTION':
                        if 'Color' in linked_node.inputs:
                            color = linked_node.inputs['Color'].default_value
                            if hasattr(color, '__len__') and len(color) >= 3:
                                absorption_color = [float(color[0]), float(color[1]), float(color[2])]
                        if 'Density' in linked_node.inputs:
                            density = max(density, float(linked_node.inputs['Density'].default_value))
            
            if density < 0.001:
                return
            
            material.volume = VolumeLayer(
                scatter_color=scatter_color,
                scatter_distance=1.0,
                absorption_color=absorption_color,
                density=density
            )
            
            logger.debug(f"  Volume (Mixed): density={density:.2f}, "
                        f"scatter={scatter_color}, absorption={absorption_color}")
        
        def _extract_meshes(self, material_map: Dict[str, int]) -> List[Mesh]:
            """
            Extract geometry from Blender objects.
            Reference: https://docs.blender.org/api/current/bpy.types.Mesh.html
            """
            meshes = []
            
            for obj in bpy.data.objects:
                if obj.type != 'MESH':
                    continue
                
                mesh_data = obj.data
                logger.debug(f"Processing mesh: {obj.name} "
                            f"({len(mesh_data.vertices)} verts, "
                            f"{len(mesh_data.polygons)} faces)")
                
                # Get material index for this mesh
                mat_idx = 0
                if obj.material_slots and len(obj.material_slots) > 0:
                    if obj.material_slots[0].material:
                        mat_name = obj.material_slots[0].material.name
                        mat_idx = material_map.get(mat_name, 0)
                        logger.debug(f"  Using material: {mat_name} (index {mat_idx})")
                
                # Ensure mesh has UV coordinates
                if not mesh_data.uv_layers:
                    mesh_data.uv_layers.new(name="UVMap")
                    logger.debug(f"  Created default UV layer for {obj.name}")
                
                # Calculate tangents (needed for normal mapping)
                try:
                    mesh_data.calc_tangents()
                except Exception as e:
                    logger.warning(f"  Failed to calculate tangents: {e}")
                
                # Create a bmesh for triangulation
                bm = bmesh.new()
                bm.from_mesh(mesh_data)
                
                # Triangulate all faces
                bmesh.ops.triangulate(bm, faces=bm.faces[:])
                
                # Write back to mesh
                bm.to_mesh(mesh_data)
                bm.free()
                
                # Recalculate tangents after triangulation
                try:
                    mesh_data.calc_tangents()
                except:
                    pass
                
                # Extract vertices and indices
                vertices, indices = self._extract_vertex_data(mesh_data, obj)
                
                if len(vertices) == 0:
                    logger.warning(f"  Skipping empty mesh: {obj.name}")
                    continue
                
                mesh = Mesh(
                    name=obj.name,
                    vertices=vertices,
                    indices=indices,
                    material_index=mat_idx
                )
                
                meshes.append(mesh)
                logger.debug(f"  Extracted: {len(vertices)} vertices, "
                            f"{len(indices) // 3} triangles")
            
            if not meshes:
                logger.error("No mesh objects found in Blender file")
            
            return meshes
        
        def _extract_vertex_data(self, mesh_data, obj) -> Tuple[List[Vertex], List[int]]:
            """
            Extract vertex and index data from Blender mesh.
            Reference: https://docs.blender.org/api/current/bpy.types.MeshVertex.html
            """
            vertices = []
            indices = []
            
            uv_layer = mesh_data.uv_layers.active.data if mesh_data.uv_layers.active else None
            
            # Get world transformation matrix
            world_matrix = obj.matrix_world
            
            # Access corner normals (Blender 4.1+ API)
            # Reference: https://docs.blender.org/api/current/bpy.types.Mesh.html#bpy.types.Mesh.corner_normals
            corner_normals = mesh_data.corner_normals
            
            # Process each polygon (all triangles after triangulation)
            for poly in mesh_data.polygons:
                for loop_idx in poly.loop_indices:
                    loop = mesh_data.loops[loop_idx]
                    vert_idx = loop.vertex_index
                    vert = mesh_data.vertices[vert_idx]
                    
                    # Transform position to world space
                    world_pos = world_matrix @ vert.co
                    position = [
                        float(world_pos.x),
                        float(world_pos.y),
                        float(world_pos.z)
                    ]
                    
                    # Transform normal to world space (use corner normals)
                    loop_normal = corner_normals[loop_idx].vector
                    world_normal = (world_matrix.to_3x3() @ loop_normal).normalized()
                    normal = [
                        float(world_normal.x),
                        float(world_normal.y),
                        float(world_normal.z)
                    ]
                    
                    # UV coordinates
                    if uv_layer:
                        uv = uv_layer[loop_idx].uv
                        texcoord = [float(uv[0]), float(uv[1])]
                    else:
                        texcoord = [0.0, 0.0]
                    
                    # Tangent (for normal mapping)
                    # Note: loop.tangent is still available in Blender 5.0.0 after calc_tangents()
                    try:
                        if loop.tangent.length > 0:
                            world_tangent = (world_matrix.to_3x3() @ loop.tangent).normalized()
                            tangent = [
                                float(world_tangent.x),
                                float(world_tangent.y),
                                float(world_tangent.z)
                            ]
                        else:
                            # Fallback: calculate tangent from UV derivatives
                            tangent = self._calculate_fallback_tangent(normal)
                    except:
                        tangent = self._calculate_fallback_tangent(normal)
                    
                    vertex = Vertex(
                        position=position,
                        normal=normal,
                        texcoord=texcoord,
                        tangent=tangent
                    )
                    
                    indices.append(len(vertices))
                    vertices.append(vertex)
            
            return vertices, indices
        
        def _calculate_fallback_tangent(self, normal: List[float]) -> List[float]:
            """Calculate a perpendicular tangent vector from normal"""
            # Convert to Vector for easier math
            n = Vector(normal)
            
            # Choose a vector not parallel to normal
            if abs(n.x) < 0.9:
                up = Vector((1.0, 0.0, 0.0))
            else:
                up = Vector((0.0, 1.0, 0.0))
            
            # Cross product to get perpendicular vector
            tangent = n.cross(up).normalized()
            
            return [float(tangent.x), float(tangent.y), float(tangent.z)]
