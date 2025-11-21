"""
Wavefront OBJ/MTL Loader
Uses PyWavefront library to parse OBJ and MTL files.
Extracts geometry and converts Phong materials to PBR approximations.
"""

import os
from pathlib import Path
from typing import List, Tuple
import logging

try:
    import pywavefront
    from pywavefront import Wavefront
except ImportError:
    raise ImportError(
        "pywavefront not installed. Install with: pip install pywavefront"
    )

from base_loader import BaseLoader, LoaderRegistry
from data_structures import (
    SceneData, Mesh, Material, Vertex, Texture,
    TransmissionLayer
)


logger = logging.getLogger(__name__)


@LoaderRegistry.register('.obj', '.mtl')
class WavefrontLoader(BaseLoader):
    """
    Loader for Wavefront OBJ/MTL files.
    Converts traditional Phong/Blinn materials to PBR approximations.
    """
    
    def supports_advanced_materials(self) -> bool:
        """OBJ/MTL format has limited PBR support, mostly converted from Phong"""
        return False  # Basic transparency only, no clearcoat/transmission/sheen
    
    def load(self) -> SceneData:
        """Load OBJ file with PyWavefront"""
        logger.info(f"Parsing OBJ file with PyWavefront: {self.filepath}")
        
        # Preprocess MTL files to remove inline comments
        self._preprocess_mtl_files()
        
        # Extract Tf values before PyWavefront parsing (PyWavefront doesn't support it)
        tf_values = self._extract_tf_values()
        
        # Parse with PyWavefront
        wavefront_scene = Wavefront(
            str(self.filepath),
            collect_faces=True,
            parse=True,
            create_materials=True
        )
        
        # Extract materials and collect textures
        materials, texture_paths = self._extract_materials(wavefront_scene, tf_values)
        self.scene.materials = materials
        
        # Build texture list
        self.scene.textures = [Texture(path=path) for path in texture_paths]
        logger.info(f"Collected {len(self.scene.textures)} unique textures")
        
        # Extract geometry
        meshes = self._extract_meshes(wavefront_scene, materials)
        self.scene.meshes = meshes
        
        # Validate and log statistics
        self.validate_scene()
        self.log_statistics()
        
        return self.scene
    
    def _preprocess_mtl_files(self):
        """Preprocess MTL files to remove inline comments that cause parsing errors"""
        # Find referenced MTL files in the OBJ file
        obj_dir = Path(self.filepath).parent
        mtl_files = []
        
        # Read OBJ to find mtllib references
        try:
            with open(self.filepath, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('mtllib '):
                        mtl_name = line[7:].strip()
                        mtl_path = obj_dir / mtl_name
                        if mtl_path.exists():
                            mtl_files.append(mtl_path)
        except Exception as e:
            logger.warning(f"Failed to read OBJ file for MTL references: {e}")
            return
        
        # Clean each MTL file
        for mtl_path in mtl_files:
            try:
                # Read original content
                with open(mtl_path, 'r', encoding='utf-8') as f:
                    lines = f.readlines()
                
                # Remove inline comments (but keep full-line comments)
                cleaned_lines = []
                for line in lines:
                    stripped = line.strip()
                    # Keep empty lines and full-line comments
                    if not stripped or stripped.startswith('#'):
                        cleaned_lines.append(line)
                        continue
                    
                    # Remove inline comments (e.g., "Ka 0.5 0.5 0.5 # comment")
                    # Split at # and take only the first part
                    if '#' in line:
                        line = line.split('#')[0].rstrip() + '\n'
                    
                    cleaned_lines.append(line)
                
                # Write back cleaned content
                with open(mtl_path, 'w', encoding='utf-8') as f:
                    f.writelines(cleaned_lines)
                
                logger.info(f"Cleaned MTL file: {mtl_path.name}")
            except Exception as e:
                logger.warning(f"Failed to clean MTL file {mtl_path}: {e}")
    
    def _extract_tf_values(self) -> dict:
        """Extract Tf (transmission filter) values from MTL files before PyWavefront parsing"""
        tf_dict = {}  # material_name -> [r, g, b]
        
        obj_dir = Path(self.filepath).parent
        try:
            with open(self.filepath, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('mtllib '):
                        mtl_name = line[7:].strip()
                        mtl_path = obj_dir / mtl_name
                        if mtl_path.exists():
                            self._parse_tf_from_mtl(mtl_path, tf_dict)
        except Exception as e:
            logger.warning(f"Failed to extract Tf values: {e}")
        
        return tf_dict
    
    def _parse_tf_from_mtl(self, mtl_path: Path, tf_dict: dict):
        """Parse Tf values from a single MTL file"""
        current_material = None
        try:
            with open(mtl_path, 'r', encoding='utf-8') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('newmtl '):
                        current_material = line[7:].strip()
                    elif line.startswith('Tf ') and current_material:
                        parts = line[3:].split()
                        if len(parts) >= 3:
                            try:
                                tf_dict[current_material] = [
                                    float(parts[0]),
                                    float(parts[1]),
                                    float(parts[2])
                                ]
                                logger.debug(f"Extracted Tf for {current_material}: {tf_dict[current_material]}")
                            except ValueError:
                                pass
        except Exception as e:
            logger.warning(f"Failed to parse Tf from {mtl_path}: {e}")
    
    def _extract_materials(self, wavefront_scene: Wavefront, tf_values: dict) -> tuple[List[Material], List[str]]:
        """Extract materials from PyWavefront scene, return materials and unique texture paths"""
        materials = []
        texture_paths = []  # Unique texture paths
        texture_map = {}    # Path -> index mapping
        
        def add_texture(tex_path: str) -> int:
            """Add texture to list and return its index"""
            if not tex_path:
                return -1
            if tex_path not in texture_map:
                texture_map[tex_path] = len(texture_paths)
                texture_paths.append(tex_path)
            return texture_map[tex_path]
        
        for mat_name, mat in wavefront_scene.materials.items():
            logger.debug(f"Processing material: {mat_name}")
            
            material = Material(name=mat_name)
            
            # Check illumination model for special handling
            illum = getattr(mat, 'illumination_model', 2)
            is_mirror = (illum == 5)  # Perfect mirror
            is_glass = (illum == 7)   # Glass (refraction + reflection)
            
            logger.debug(f"  illum mode: {illum} (mirror={is_mirror}, glass={is_glass})")
            
            # Diffuse color -> base color
            # For mirror materials, prefer specular color as base_color
            if is_mirror and hasattr(mat, 'specular') and mat.specular:
                material.base_color = [
                    float(mat.specular[0]),
                    float(mat.specular[1]),
                    float(mat.specular[2])
                ]
                logger.debug(f"  Using specular as base_color for mirror: {material.base_color}")
            elif hasattr(mat, 'diffuse') and mat.diffuse:
                material.base_color = [
                    float(mat.diffuse[0]),
                    float(mat.diffuse[1]),
                    float(mat.diffuse[2])
                ]
            
            # Specular -> metallic (heuristic conversion)
            # High specular intensity suggests metallic surface
            has_specular = False
            spec_intensity = 0.0
            if hasattr(mat, 'specular') and mat.specular:
                spec_intensity = sum(mat.specular[:3]) / 3.0
                has_specular = spec_intensity > 0.01  # Consider > 0.01 as having specular
                
                # Mirror materials (illum 5) should be highly metallic
                if is_mirror:
                    material.metallic = 1.0
                    logger.debug(f"  Metallic: 1.0 (mirror material, illum=5)")
                elif spec_intensity > 0.5:
                    material.metallic = min(spec_intensity, 1.0)
                    logger.debug(f"  Metallic (from specular): {material.metallic:.2f}")
            
            # Shininess -> roughness conversion
            # If Ks = 0 (no specular), material is purely diffuse, set roughness = 1.0
            if is_mirror:
                # Mirror materials should have very low roughness
                material.roughness = 0.0
                logger.debug(f"  Roughness: 0.0 (mirror material, illum=5)")
            elif not has_specular:
                material.roughness = 1.0
                logger.debug(f"  Roughness: 1.0 (purely diffuse, no specular)")
            elif hasattr(mat, 'shininess') and mat.shininess > 0:
                # Phong to PBR roughness: roughness = sqrt(2/(shininess+2))
                material.roughness = (2.0 / (mat.shininess + 2.0)) ** 0.5
                logger.debug(f"  Roughness (from shininess {mat.shininess}): {material.roughness:.2f}")
            else:
                material.roughness = 1.0  # Default to diffuse
                logger.debug(f"  Roughness: 1.0 (default)")
            
            # Emission (Ke in MTL)
            if hasattr(mat, 'emissive') and mat.emissive:
                material.emission = [
                    float(mat.emissive[0]),
                    float(mat.emissive[1]),
                    float(mat.emissive[2])
                ]
                emission_intensity = sum(material.emission) / 3.0
                if emission_intensity > 0.01:
                    logger.debug(f"  Emission: {material.emission} (intensity: {emission_intensity:.2f})")
            
            # Opacity/transparency
            # Get Tf (transmission filter) color if available
            tf_color = tf_values.get(mat_name, [1.0, 1.0, 1.0])
            
            # For glass materials (illum 7), we should enable transmission
            if is_glass:
                # Glass material - high transmission
                material.opacity = 0.1  # Very transparent
                transmission_strength = 0.9
                material.transmission = TransmissionLayer(
                    strength=transmission_strength,
                    roughness=material.roughness,
                    depth=0.0,
                    color=tf_color,  # Use Tf color for transmission filter
                    texture_index=-1
                )
                logger.debug(f"  Glass material: transmission={transmission_strength:.2f}, opacity={material.opacity:.2f}, Tf={tf_color}")
            elif hasattr(mat, 'transparency'):
                material.opacity = float(mat.transparency)
                
                # Add transmission layer for transparent materials
                if material.opacity < 0.99:
                    transmission_strength = 1.0 - material.opacity
                    material.transmission = TransmissionLayer(
                        strength=transmission_strength,
                        roughness=material.roughness,
                        depth=0.0,
                        color=tf_color,  # Use Tf color for transmission filter
                        texture_index=-1
                    )
                    logger.debug(f"  Transmission layer added (strength: {transmission_strength:.2f}, Tf={tf_color})")
            
            # IOR (optical density)
            if hasattr(mat, 'optical_density') and mat.optical_density > 1.0:
                material.ior = float(mat.optical_density)
                logger.debug(f"  IOR: {material.ior:.2f}")
            
            # Textures
            if hasattr(mat, 'texture') and mat.texture:
                tex_path = self._resolve_texture_path(mat.texture)
                if tex_path:
                    material.base_color_texture = add_texture(tex_path)
                    logger.debug(f"  Base color texture[{material.base_color_texture}]: {tex_path}")
            
            # Check for other texture maps
            if hasattr(mat, 'texture_normal') and mat.texture_normal:
                tex_path = self._resolve_texture_path(mat.texture_normal)
                material.normal_texture = add_texture(tex_path)
                
            if hasattr(mat, 'texture_metallic') and mat.texture_metallic:
                tex_path = self._resolve_texture_path(mat.texture_metallic)
                material.metallic_roughness_texture = add_texture(tex_path)
                
            if hasattr(mat, 'texture_emission') and mat.texture_emission:
                tex_path = self._resolve_texture_path(mat.texture_emission)
                material.emission_texture = add_texture(tex_path)
            
            materials.append(material)
        
        # Add default material if none exist
        if not materials:
            logger.warning("No materials found in OBJ file, adding default material")
            materials.append(Material(name="default"))
        
        return materials, texture_paths
    
    def _resolve_texture_path(self, texture) -> str:
        """Resolve texture path relative to OBJ file directory"""
        # Extract texture filename
        if hasattr(texture, 'path'):
            tex_filename = texture.path
        elif isinstance(texture, str):
            tex_filename = texture
        else:
            tex_filename = str(texture)
        
        # Resolve relative to OBJ directory
        obj_dir = self.filepath.parent
        tex_path = obj_dir / tex_filename
        
        if tex_path.exists():
            return str(tex_path.resolve())
        else:
            logger.warning(f"Texture not found: {tex_path}")
            return ""
    
    def _extract_meshes(
        self,
        wavefront_scene: Wavefront,
        materials: List[Material]
    ) -> List[Mesh]:
        """Extract geometry from PyWavefront scene"""
        meshes = []
        material_map = {mat.name: idx for idx, mat in enumerate(materials)}
        
        for mesh_name, mesh_mat in wavefront_scene.materials.items():
            if not hasattr(mesh_mat, 'vertices') or not mesh_mat.vertices:
                continue
            
            logger.debug(f"Processing mesh: {mesh_name}")
            
            # Determine vertex stride
            vertex_format = mesh_mat.vertex_format
            # Format example: "V3F N3F T2F" means 3+3+2=8 floats per vertex
            stride = self._calculate_stride(vertex_format)
            
            # Extract vertices
            vertices_flat = mesh_mat.vertices
            num_vertices = len(vertices_flat) // stride
            
            if num_vertices == 0:
                logger.warning(f"Mesh {mesh_name} has no vertices, skipping")
                continue
            
            vertices = []
            for i in range(num_vertices):
                base_idx = i * stride
                vertex = self._extract_vertex(vertices_flat, base_idx, stride, vertex_format)
                vertices.append(vertex)
            
            # Generate indices (assuming already triangulated)
            indices = list(range(num_vertices))
            
            # Check if normals need to be computed
            needs_normal_computation = 'N' not in vertex_format.upper()
            if needs_normal_computation:
                logger.debug(f"  Computing normals for {mesh_name} (format: {vertex_format})")
                vertices = self._compute_normals(vertices, indices)
            
            # Create mesh
            mesh = Mesh(
                name=mesh_name,
                vertices=vertices,
                indices=indices,
                material_index=material_map.get(mesh_name, 0)
            )
            
            meshes.append(mesh)
            logger.debug(f"  Vertices: {num_vertices}, Material: {mesh.material_index}")
        
        return meshes
    
    def _calculate_stride(self, vertex_format: str) -> int:
        """Calculate vertex stride from format string (e.g., 'V3F_N3F_T2F' or 'V3F N3F T2F' -> 8)"""
        # Parse format like "V3F N3F T2F" or "V3F_N3F_T2F"
        # Split by both space and underscore
        tokens = vertex_format.replace('_', ' ').split()
        stride = 0
        for token in tokens:
            # Extract number (e.g., "V3F" -> 3)
            num_str = ''.join(c for c in token if c.isdigit())
            if num_str:
                stride += int(num_str)
        
        # Default to 8 if parsing fails (V3F N3F T2F)
        return stride if stride > 0 else 8
    
    def _extract_vertex(
        self,
        vertices_flat: List[float],
        base_idx: int,
        stride: int,
        vertex_format: str
    ) -> Vertex:
        """Extract single vertex from flat vertex array based on format"""
        # Parse vertex format to determine component order
        # Common formats: "T2F_N3F_V3F", "N3F_V3F", "V3F_N3F_T2F"
        components = vertex_format.replace('_', ' ').split()
        
        position = [0.0, 0.0, 0.0]
        normal = [0.0, 0.0, 1.0]
        texcoord = [0.0, 0.0]
        
        offset = 0
        for comp in components:
            comp_upper = comp.upper()
            
            if comp_upper.startswith('V'):
                # Position (Vertex)
                size = int(''.join(c for c in comp if c.isdigit()))
                position = [float(vertices_flat[base_idx + offset + i]) for i in range(size)]
                if size == 2:
                    position.append(0.0)  # Add z=0 for 2D positions
                offset += size
                
            elif comp_upper.startswith('N'):
                # Normal
                size = int(''.join(c for c in comp if c.isdigit()))
                normal = [float(vertices_flat[base_idx + offset + i]) for i in range(size)]
                offset += size
                
            elif comp_upper.startswith('T') or comp_upper.startswith('C'):
                # TexCoord (T) or Color (C)
                size = int(''.join(c for c in comp if c.isdigit()))
                if comp_upper.startswith('T'):
                    texcoord = [float(vertices_flat[base_idx + offset + i]) for i in range(size)]
                offset += size
                
            else:
                # Unknown component, try to extract size and skip
                num_str = ''.join(c for c in comp if c.isdigit())
                if num_str:
                    offset += int(num_str)
        
        # Tangent (not provided by OBJ, use default)
        tangent = [1.0, 0.0, 0.0]
        
        return Vertex(
            position=position,
            normal=normal,
            texcoord=texcoord,
            tangent=tangent
        )
    
    def _compute_normals(self, vertices: List[Vertex], indices: List[int]) -> List[Vertex]:
        """Compute normals from triangle geometry for vertices without normals"""
        import numpy as np
        
        # Initialize accumulated normals
        num_verts = len(vertices)
        accumulated_normals = [np.array([0.0, 0.0, 0.0]) for _ in range(num_verts)]
        
        # Compute face normals and accumulate to vertices
        num_triangles = len(indices) // 3
        for i in range(num_triangles):
            i0, i1, i2 = indices[i*3], indices[i*3+1], indices[i*3+2]
            
            v0 = np.array(vertices[i0].position)
            v1 = np.array(vertices[i1].position)
            v2 = np.array(vertices[i2].position)
            
            # Compute face normal
            edge1 = v1 - v0
            edge2 = v2 - v0
            face_normal = np.cross(edge1, edge2)
            
            # Accumulate to vertices
            accumulated_normals[i0] += face_normal
            accumulated_normals[i1] += face_normal
            accumulated_normals[i2] += face_normal
        
        # Normalize and update vertices
        new_vertices = []
        for i, vertex in enumerate(vertices):
            normal = accumulated_normals[i]
            norm = np.linalg.norm(normal)
            if norm > 1e-6:
                normal = normal / norm
            else:
                normal = np.array([0.0, 0.0, 1.0])
            
            new_vertices.append(Vertex(
                position=vertex.position,
                normal=normal.tolist(),
                texcoord=vertex.texcoord,
                tangent=vertex.tangent
            ))
        
        return new_vertices
