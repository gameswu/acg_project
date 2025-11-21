"""
Base loader class and loader registry using factory pattern and decorators.
Provides extensible architecture for adding new format loaders.
"""

from abc import ABC, abstractmethod
from typing import Dict, Type, List, Optional
from pathlib import Path
import logging

from data_structures import SceneData


# Configure logging
logging.basicConfig(level=logging.INFO, format='[%(levelname)s] %(message)s')
logger = logging.getLogger(__name__)


class LoaderRegistry:
    """
    Registry for scene loaders using factory pattern.
    Allows registration of loaders for specific file extensions.
    """
    _loaders: Dict[str, Type['BaseLoader']] = {}
    
    @classmethod
    def register(cls, *extensions: str):
        """
        Decorator to register a loader for specific file extensions.
        
        Usage:
            @LoaderRegistry.register('.obj', '.mtl')
            class WavefrontLoader(BaseLoader):
                ...
        """
        def decorator(loader_class: Type['BaseLoader']):
            for ext in extensions:
                ext_lower = ext.lower()
                if ext_lower in cls._loaders:
                    logger.warning(f"Overwriting existing loader for {ext_lower}")
                cls._loaders[ext_lower] = loader_class
                logger.debug(f"Registered {loader_class.__name__} for {ext_lower}")
            return loader_class
        return decorator
    
    @classmethod
    def get_loader(cls, filepath: str) -> Optional[Type['BaseLoader']]:
        """Get appropriate loader class for file extension"""
        ext = Path(filepath).suffix.lower()
        loader_class = cls._loaders.get(ext)
        
        if loader_class is None:
            logger.error(f"No loader registered for extension: {ext}")
            logger.info(f"Supported extensions: {', '.join(cls._loaders.keys())}")
        
        return loader_class
    
    @classmethod
    def list_supported_formats(cls) -> List[str]:
        """List all supported file extensions"""
        return sorted(cls._loaders.keys())
    
    @classmethod
    def create_loader(cls, filepath: str) -> Optional['BaseLoader']:
        """Factory method: create loader instance for given file"""
        loader_class = cls.get_loader(filepath)
        if loader_class:
            return loader_class(filepath)
        return None


class BaseLoader(ABC):
    """
    Abstract base class for scene loaders.
    All format-specific loaders should inherit from this class.
    """
    
    def __init__(self, filepath: str):
        self.filepath = Path(filepath)
        self.scene = SceneData()
        
        if not self.filepath.exists():
            raise FileNotFoundError(f"File not found: {filepath}")
        
        logger.info(f"Initialized {self.__class__.__name__} for {self.filepath.name}")
    
    @abstractmethod
    def load(self) -> SceneData:
        """
        Load scene data from file.
        Must be implemented by subclasses.
        
        Returns:
            SceneData: Loaded scene with meshes, materials, and textures
        """
        pass
    
    @abstractmethod
    def supports_advanced_materials(self) -> bool:
        """
        Indicate if this loader supports advanced material layers.
        
        Returns:
            bool: True if loader can extract clearcoat, transmission, etc.
        """
        pass
    
    def get_format_name(self) -> str:
        """Get human-readable format name"""
        return self.__class__.__name__.replace('Loader', '')
    
    def validate_scene(self) -> bool:
        """
        Validate loaded scene data.
        Can be overridden by subclasses for format-specific validation.
        
        Returns:
            bool: True if scene is valid
        """
        if not self.scene.meshes:
            logger.warning("Scene has no meshes")
            return False
        
        if not self.scene.materials:
            logger.warning("Scene has no materials, will use default")
            # Add default material
            from data_structures import Material
            self.scene.materials.append(Material(name="Default"))
        
        # Validate material indices
        num_materials = len(self.scene.materials)
        for mesh in self.scene.meshes:
            if mesh.material_index >= num_materials:
                logger.error(
                    f"Mesh '{mesh.name}' references invalid material index "
                    f"{mesh.material_index} (max: {num_materials - 1})"
                )
                return False
        
        logger.info(f"Scene validation passed: "
                   f"{len(self.scene.meshes)} meshes, "
                   f"{len(self.scene.materials)} materials, "
                   f"{len(self.scene.textures)} textures")
        
        return True
    
    def log_statistics(self):
        """Log scene loading statistics"""
        total_vertices = sum(len(mesh.vertices) for mesh in self.scene.meshes)
        total_triangles = sum(len(mesh.indices) // 3 for mesh in self.scene.meshes)
        
        logger.info("=" * 60)
        logger.info(f"Loaded {self.filepath.name} ({self.get_format_name()} format)")
        logger.info(f"  Meshes:    {len(self.scene.meshes)}")
        logger.info(f"  Vertices:  {total_vertices:,}")
        logger.info(f"  Triangles: {total_triangles:,}")
        logger.info(f"  Materials: {len(self.scene.materials)}")
        logger.info(f"  Textures:  {len(self.scene.textures)}")
        
        # Log material types
        if self.supports_advanced_materials():
            advanced_count = sum(
                1 for mat in self.scene.materials 
                if mat.get_layer_flags() != 0
            )
            logger.info(f"  Advanced materials: {advanced_count}/{len(self.scene.materials)}")
        
        logger.info("=" * 60)
