"""
Scene data structures for transferring model/material data to C++ renderer.
These classes represent the intermediate format between Python loaders and C++ renderer.
"""

from dataclasses import dataclass, field, asdict
from typing import List, Optional, Dict, Any


@dataclass
class Vertex:
    """Vertex data structure matching C++ Vertex struct"""
    position: List[float]  # [x, y, z]
    normal: List[float]    # [nx, ny, nz]
    texcoord: List[float]  # [u, v]
    tangent: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])


@dataclass
class Mesh:
    """Mesh data structure"""
    name: str
    vertices: List[Vertex]
    indices: List[int]
    material_index: int = 0


@dataclass
class ClearcoatLayer:
    """Clearcoat layer for multi-layer materials"""
    strength: float = 0.0
    roughness: float = 0.0
    ior: float = 1.5
    texture_index: int = -1
    color: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    padding: float = 0.0


@dataclass
class TransmissionLayer:
    """Transmission layer for glass/transparent materials"""
    strength: float = 0.0
    roughness: float = 0.0
    depth: float = 0.0
    texture_index: int = -1
    color: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    padding: float = 0.0


@dataclass
class SheenLayer:
    """Sheen layer for fabric materials"""
    strength: float = 0.0
    roughness: float = 0.0
    tint: float = 0.0
    texture_index: int = -1
    color: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    padding: float = 0.0


@dataclass
class SubsurfaceLayer:
    """Subsurface scattering layer"""
    strength: float = 0.0
    radius: float = 1.0
    scale: float = 1.0
    texture_index: int = -1
    color: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    padding: float = 0.0


@dataclass
class AnisotropyLayer:
    """Anisotropic reflection layer"""
    strength: float = 0.0
    rotation: float = 0.0
    padding0: float = 0.0
    texture_index: int = -1
    tangent: List[float] = field(default_factory=lambda: [1.0, 0.0, 0.0])
    padding1: float = 0.0


@dataclass
class IridescenceLayer:
    """Thin-film iridescence layer"""
    strength: float = 0.0
    ior: float = 1.3
    thickness: float = 400.0
    texture_index: int = -1
    padding: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0, 0.0])


@dataclass
class VolumeLayer:
    """Volume absorption layer"""
    density: float = 0.0
    anisotropy: float = 0.0
    padding0: float = 0.0
    padding1: float = 0.0
    absorption_color: List[float] = field(default_factory=lambda: [1.0, 1.0, 1.0])
    padding2: float = 0.0


@dataclass
class Material:
    """
    PBR Material with optional advanced layers.
    Matches C++ MaterialData structure (64 bytes base + 32 bytes per layer).
    """
    name: str
    
    # Base PBR properties (32 bytes packed in C++)
    base_color: List[float] = field(default_factory=lambda: [0.8, 0.8, 0.8])
    metallic: float = 0.0
    emission: List[float] = field(default_factory=lambda: [0.0, 0.0, 0.0])
    roughness: float = 0.5
    
    # Extended properties (32 bytes packed in C++)
    ior: float = 1.5
    opacity: float = 1.0
    layer_flags: int = 0  # Bit flags for enabled layers
    extended_data_index: int = 0  # Index into layer buffer
    
    # Texture indices (16 bytes in C++)
    base_color_texture: int = -1
    normal_texture: int = -1
    metallic_roughness_texture: int = -1
    emission_texture: int = -1
    
    # Advanced layers (optional, 32 bytes each)
    clearcoat: Optional[ClearcoatLayer] = None
    transmission: Optional[TransmissionLayer] = None
    sheen: Optional[SheenLayer] = None
    subsurface: Optional[SubsurfaceLayer] = None
    anisotropy: Optional[AnisotropyLayer] = None
    iridescence: Optional[IridescenceLayer] = None
    volume: Optional[VolumeLayer] = None
    
    def get_layer_flags(self) -> int:
        """Calculate layer flags bitmap"""
        flags = 0
        if self.clearcoat and self.clearcoat.strength > 0.0:
            flags |= (1 << 0)  # LAYER_CLEARCOAT
        if self.transmission and self.transmission.strength > 0.0:
            flags |= (1 << 1)  # LAYER_TRANSMISSION
        if self.sheen and self.sheen.strength > 0.0:
            flags |= (1 << 2)  # LAYER_SHEEN
        if self.subsurface and self.subsurface.strength > 0.0:
            flags |= (1 << 3)  # LAYER_SUBSURFACE
        if self.anisotropy and self.anisotropy.strength > 0.0:
            flags |= (1 << 4)  # LAYER_ANISOTROPY
        if self.iridescence and self.iridescence.strength > 0.0:
            flags |= (1 << 5)  # LAYER_IRIDESCENCE
        if self.volume and self.volume.density > 0.0:
            flags |= (1 << 6)  # LAYER_VOLUME
        return flags


@dataclass
class Texture:
    """Texture metadata"""
    path: str
    width: int = 0
    height: int = 0


@dataclass
class SceneData:
    """Complete scene data for C++ renderer"""
    meshes: List[Mesh] = field(default_factory=list)
    materials: List[Material] = field(default_factory=list)
    textures: List[Texture] = field(default_factory=list)
    
    # Binary export only - use binary_exporter.py
