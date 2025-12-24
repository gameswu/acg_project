"""
Wavefront OBJ/MTL Loader
Uses PyWavefront library to parse OBJ and MTL files.
Extracts geometry and converts Phong materials to PBR approximations.
"""

import os
import sys
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
        # Parsing OBJ file (silent)
        
        # Preprocess MTL files to remove inline comments
        self._preprocess_mtl_files()
        
        # Extract Tf values before PyWavefront parsing (PyWavefront doesn't support it)
        tf_values = self._extract_tf_values()
        
        # Extract material usage order from OBJ file BEFORE PyWavefront parsing
        # PyWavefront creates materials dict in MTL definition order,
        # but we need OBJ usage order to match mesh material indices
        material_usage_order = self._get_material_usage_order()
        
        # Parse with PyWavefront
        wavefront_scene = Wavefront(
            str(self.filepath),
            collect_faces=True,
            parse=True,
            create_materials=True
        )
        
        # Extract materials and collect textures
        materials, texture_paths = self._extract_materials(wavefront_scene, tf_values, material_usage_order)
        self.scene.materials = materials
        
        # Build texture list
        self.scene.textures = [Texture(path=path) for path in texture_paths]
        # Textures collected (silent)
        
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
            pass  # Failed to read MTL references (silent)
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
                
                pass  # MTL file cleaned (silent)
            except Exception as e:
                pass  # Failed to clean MTL (silent)
    
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
            pass  # Failed to extract Tf (silent)
        
        return tf_dict
    
    def _get_material_usage_order(self) -> List[str]:
        """Extract material names in OBJ file usage order (first usemtl appearance)
        
        CRITICAL: This determines the material index order. Materials must be sorted
        by first appearance in OBJ file, NOT by MTL file definition order.
        PyWavefront's materials dict uses MTL order, which doesn't match usage order.
        """
        material_order = []
        seen = set()
        
        try:
            with open(self.filepath, 'r', encoding='utf-8', errors='ignore') as f:
                for line in f:
                    line = line.strip()
                    if line.startswith('usemtl '):
                        mat_name = line[7:].strip()
                        if mat_name and mat_name not in seen:
                            material_order.append(mat_name)
                            seen.add(mat_name)
        except Exception as e:
            logger.error(f"Failed to extract material usage order: {e}")
            return []
        
        logger.info(f"Extracted {len(material_order)} materials in OBJ usage order")
        if material_order:
            logger.debug(f"First 5 materials in usage order: {material_order[:5]}")
        
        return material_order
    
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
            pass  # Failed to parse Tf (silent)
    
    def _extract_materials(self, wavefront_scene: Wavefront, tf_values: dict, material_usage_order: List[str]) -> tuple[List[Material], List[str]]:
        """Extract materials from PyWavefront scene, return materials and unique texture paths
        
        CRITICAL: Materials must be ordered by OBJ usage order (first usemtl appearance),
        NOT by MTL file definition order, to match mesh material indices.
        
        Args:
            wavefront_scene: Parsed PyWavefront scene
            tf_values: Transmission filter values extracted from MTL
            material_usage_order: Material names in OBJ file usage order
        
        Returns:
            (materials_list, texture_paths_list) both ordered by usage order
        """
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
        
        # Iterate materials in OBJ usage order, NOT PyWavefront dict order
        if not material_usage_order:
            logger.warning("No material usage order provided, falling back to PyWavefront dict order (may cause texture mapping issues!)")
            material_usage_order = list(wavefront_scene.materials.keys())
        
        for mat_name in material_usage_order:
            # Get material from PyWavefront scene
            if mat_name not in wavefront_scene.materials:
                logger.warning(f"Material '{mat_name}' used in OBJ but not defined in MTL, skipping")
                # Add a default material to maintain index alignment
                materials.append(Material(name=mat_name))
                continue
            
            mat = wavefront_scene.materials[mat_name]
            logger.debug(f"Processing material: {mat_name}")
            
            material = Material(name=mat_name)
            
            # Check illumination model for special handling
            illum = getattr(mat, 'illumination_model', 2)
            
            # illum 5 can be mirror OR glass - check IOR to determine
            # If material has IOR > 1.0, treat as glass (like water), otherwise treat as mirror
            ior_value = getattr(mat, 'optical_density', 1.0)
            is_mirror = False
            is_glass = False
            
            # Get Tf (transmission filter) color FIRST for base_color decision
            tf_color = tf_values.get(mat_name, [1.0, 1.0, 1.0])
            tf_avg = sum(tf_color) / 3.0
            
            # DEBUG: Print to stderr to bypass logging level filter
            print(f"[DEBUG] Material '{mat_name}': illum={illum}, IOR={ior_value:.2f}, Tf={tf_avg:.2f}", file=sys.stderr)
            
            if illum == 5:
                if ior_value > 1.01:  # Has meaningful refraction
                    is_glass = True
                    print(f"[DEBUG]   → Treating as GLASS (has refraction)", file=sys.stderr)
                else:
                    is_mirror = True
                    print(f"[DEBUG]   → Treating as MIRROR (no refraction)", file=sys.stderr)
            elif illum == 7:
                is_glass = True
            elif illum == 3:
                is_mirror = True
            
            logger.debug(f"  illum mode: {illum} (mirror={is_mirror}, glass={is_glass}, IOR={ior_value:.2f})")
            
            # Diffuse color -> base color
            # For mirror materials, prefer specular color as base_color
            if is_mirror and hasattr(mat, 'specular') and mat.specular:
                material.base_color = [
                    float(mat.specular[0]),
                    float(mat.specular[1]),
                    float(mat.specular[2])
                ]
                logger.debug(f"  Using specular as base_color for mirror: {material.base_color}")
            elif is_glass:
                # For highly transparent glass (Tf > 0.7), use white base color
                # For water-like materials (Tf < 0.7), use tinted base color
                if tf_avg > 0.7:
                    material.base_color = [0.95, 0.95, 0.95]  # Nearly white for transparent glass
                    print(f"[DEBUG]   → Using white base_color for transparent glass (Tf={tf_avg:.2f})", file=sys.stderr)
                else:
                    material.base_color = [0.15, 0.25, 0.30]  # Blue-green tint for water
                    print(f"[DEBUG]   → Using tinted base_color for water (Tf={tf_avg:.2f})", file=sys.stderr)
            elif hasattr(mat, 'diffuse') and mat.diffuse:
                material.base_color = [
                    float(mat.diffuse[0]),
                    float(mat.diffuse[1]),
                    float(mat.diffuse[2])
                ]
                
                # CRITICAL FIX: If Kd is black (0,0,0) but material has texture,
                # use white (1,1,1) as base color so texture is visible
                # This is common in Blender exports where texture fully replaces color
                if hasattr(mat, 'texture') and mat.texture:
                    kd_sum = sum(material.base_color)
                    if kd_sum < 0.01:  # Essentially black
                        material.base_color = [1.0, 1.0, 1.0]
                        pass  # Using white base_color for textured material (silent)
            
            # Specular -> metallic (heuristic conversion)
            # High specular intensity suggests metallic surface
            has_specular = False
            spec_intensity = 0.0
            if hasattr(mat, 'specular') and mat.specular:
                spec_intensity = sum(mat.specular[:3]) / 3.0
                has_specular = spec_intensity > 0.01  # Consider > 0.01 as having specular
                
                # Mirror materials (illum 5 without IOR) should be highly metallic
                # Glass materials (illum 5 with IOR) should be dielectric (non-metallic)
                if is_mirror:
                    material.metallic = 1.0
                    logger.debug(f"  Metallic: 1.0 (mirror material)")
                elif is_glass:
                    material.metallic = 0.0  # Dielectric, not metallic
                    logger.debug(f"  Metallic: 0.0 (glass material)")
                elif spec_intensity > 0.5:
                    material.metallic = min(spec_intensity, 1.0)
                    logger.debug(f"  Metallic (from specular): {material.metallic:.2f}")
            
            # Shininess -> roughness conversion
            # If Ks = 0 (no specular), material is purely diffuse, set roughness = 1.0
            if is_mirror:
                # Mirror materials should have very low roughness
                material.roughness = 0.0
                logger.debug(f"  Roughness: 0.0 (mirror material)")
            elif is_glass:
                # Glass materials: slightly rough for realistic surface
                material.roughness = 0.05  # Very smooth but not perfect
                logger.debug(f"  Roughness: 0.05 (glass material)")
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
            # Tf color already extracted above
            
            # Check if material is actually transparent (not just illum mode)
            # Only treat as transparent if:
            # 1. Has explicit transparency value (d < 0.99)
            # 2. OR Tf (transmission filter) is NOT white (colored transmission)
            kd_avg = sum(material.base_color) / 3.0 if material.base_color else 1.0
            has_explicit_transparency = hasattr(mat, 'transparency') and mat.transparency < 0.99
            has_colored_transmission = tf_avg < 0.99  # Tf != white means colored glass
            is_actually_transparent = has_explicit_transparency or has_colored_transmission
            
            if mat_name in ['breakfast_room:Artwork', 'breakfast_room:Floor_Tiles']:
                pass  # Transparency check (silent)
            
            # For glass materials (illum 7) OR illum 4/6 with actual transparency, enable transmission
            if is_glass or (illum in [4, 6] and is_actually_transparent):
                # Glass material - use Tf value to control transparency
                # Tf close to 1.0 = highly transparent, Tf close to 0.0 = opaque
                # opacity: lower = more transparent, higher = more reflective
                # transmission_strength: higher = more light passes through
                material.opacity = 1.0 - tf_avg  # Tf=0.85 → opacity=0.15 (very transparent)
                transmission_strength = tf_avg  # Tf=0.85 → strength=0.85 (high transmission)
                print(f"[DEBUG]   → Glass transmission: Tf={tf_avg:.2f}, opacity={material.opacity:.2f}, strength={transmission_strength:.2f}", file=sys.stderr)
                material.transmission = TransmissionLayer(
                    strength=transmission_strength,
                    roughness=material.roughness,
                    depth=0.0,
                    color=tf_color,  # Use Tf color for transmission filter
                    texture_index=-1
                )
                pass  # Added transmission layer (silent)
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
                    logger.info(f"  ✓ Base color texture[{material.base_color_texture}]: {tex_path}")
                else:
                    logger.warning(f"  ✗ Base color texture path could not be resolved: {mat.texture}")
            
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
        
        # Convert to Path object
        tex_path = Path(tex_filename)
        
        # If the path is already absolute and exists, use it directly
        if tex_path.is_absolute() and tex_path.exists():
            return str(tex_path.resolve())
        
        # Check if the path exists as-is (PyWavefront may already resolve it)
        if tex_path.exists():
            return str(tex_path.resolve())
        
        # Otherwise, resolve relative to OBJ directory
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
        print(f"[DEBUG] _extract_meshes called, materials dict size: {len(wavefront_scene.materials)}")
        meshes = []
        material_map = {mat.name: idx for idx, mat in enumerate(materials)}
        
        for mesh_name, mesh_mat in wavefront_scene.materials.items():
            if not hasattr(mesh_mat, 'vertices') or not mesh_mat.vertices:
                continue
            
            print(f"[DEBUG] Processing mesh: {mesh_name}")
            logger.info(f"Processing mesh: {mesh_name}")
            
            # Determine vertex stride
            vertex_format = mesh_mat.vertex_format
            print(f"[DEBUG]   Vertex format: {vertex_format}")
            logger.info(f"  Vertex format: {vertex_format}")
            # Format example: "V3F N3F T2F" means 3+3+2=8 floats per vertex
            stride = self._calculate_stride(vertex_format)
            print(f"[DEBUG]   Calculated stride: {stride}")
            logger.info(f"  Calculated stride: {stride}")
            
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
            
            # ALWAYS compute smooth normals for meshes without explicit normals
            # Check if normals need to be computed (OBJ files without vn lines)
            needs_normal_computation = 'N' not in vertex_format.upper()
            
            print(f"[DEBUG]   Needs normal computation: {needs_normal_computation}")
            logger.info(f"  Needs normal computation: {needs_normal_computation}")
            
            if needs_normal_computation:
                print(f"[DEBUG]   Format '{vertex_format}' has no normals, computing smooth normals")
                logger.info(f"  Format '{vertex_format}' has no normals, computing smooth normals for {mesh_name}")
            
            # Also check if all normals are identical (PyWavefront may generate flat normals)
            if not needs_normal_computation and len(vertices) > 3:
                first_normal = vertices[0].normal
                print(f"[DEBUG]   First normal: {first_normal}")
                # Check first 20 vertices to detect flat normals
                sample_size = min(20, len(vertices))
                all_same = all(
                    abs(v.normal[0] - first_normal[0]) < 0.001 and
                    abs(v.normal[1] - first_normal[1]) < 0.001 and
                    abs(v.normal[2] - first_normal[2]) < 0.001
                    for v in vertices[:sample_size]
                )
                print(f"[DEBUG]   All normals same: {all_same}")
                if all_same:
                    print(f"[DEBUG]   Detected flat normals ({first_normal}), recomputing smooth normals")
                    logger.info(f"  Detected flat normals ({first_normal}) in {mesh_name}, recomputing smooth normals")
                    needs_normal_computation = True
                else:
                    print(f"[DEBUG]   Normals vary, keeping original")
                    logger.info(f"  Format '{vertex_format}' has varying normals, keeping original normals for {mesh_name}")
            
            if needs_normal_computation:
                print(f"[DEBUG]   Computing smooth normals for {num_vertices} vertices")
                logger.info(f"  Computing smooth normals for {mesh_name} ({num_vertices} vertices)")
                vertices = self._compute_normals(vertices, indices)
                print(f"[DEBUG]   Smooth normals computed")
                logger.info(f"  Smooth normals computed for {mesh_name}")
            
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
