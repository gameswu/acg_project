#!/usr/bin/env python3
"""
Main Scene Loader Entry Point
Automatically detects file format and uses appropriate loader.
Outputs binary ACG scene data for C++ renderer.

Usage:
    python main.py <input_file> <output_acg>
    
Examples:
    python main.py model.obj scene.acg
    python main.py model.blend scene.acg
"""

import sys
import os
from pathlib import Path
import logging
from typing import Optional

# Import loader system
from base_loader import LoaderRegistry, BaseLoader
from data_structures import SceneData

# Import all loaders to trigger registration
import wavefront_loader

# Try to import bpy_loader (optional, only needed for .blend files)
try:
    import bpy_loader
    HAS_BPY_LOADER = True
except ImportError as e:
    HAS_BPY_LOADER = False
    logger = logging.getLogger(__name__)
    logger.warning(f"Blender loader not available (bpy not installed). Only OBJ files supported.")
    logger.debug(f"Import error: {e}")


# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='[%(levelname)s] %(message)s'
)
logger = logging.getLogger(__name__)


def load_scene(input_file: str, output_file: str) -> bool:
    """
    Main loading function using factory pattern.
    
    Args:
        input_file: 输入模型文件路径
        output_file: 输出ACG二进制文件路径
    
    Returns:
        bool: True if successful, False otherwise
    """
    input_path = Path(input_file)
    output_path = Path(output_file)
    
    # 强制使用二进制格式
    if not output_path.suffix == '.acg':
        logger.warning(f"Output file should have .acg extension, got: {output_path.suffix}")
    
    # Validate input file
    if not input_path.exists():
        logger.error(f"Input file not found: {input_file}")
        return False
    
    # Get appropriate loader via factory
    loader = LoaderRegistry.create_loader(str(input_path))
    
    if loader is None:
        logger.error(f"No loader available for file: {input_file}")
        logger.info(f"Supported formats: {', '.join(LoaderRegistry.list_supported_formats())}")
        return False
    
    try:
        # Load scene
        logger.info(f"Loading scene from: {input_file}")
        logger.info(f"Using loader: {loader.get_format_name()}")
        
        scene = loader.load()
        
        # Save to binary format
        logger.info(f"Saving scene to binary format: {output_file}")
        from binary_exporter import BinarySceneExporter
        exporter = BinarySceneExporter()
        exporter.export(scene, str(output_path))
        
        logger.info("✓ Scene loading completed successfully")
        return True
        
    except Exception as e:
        logger.error(f"Failed to load scene: {e}")
        import traceback
        traceback.print_exc()
        return False


def print_usage():
    """Print usage information"""
    print(__doc__)
    print("\nSupported Formats:")
    for ext in LoaderRegistry.list_supported_formats():
        loader_class = LoaderRegistry.get_loader(f"test{ext}")
        if loader_class:
            loader_name = loader_class.__name__
            print(f"  {ext:10} -> {loader_name}")


def main():
    """Main entry point"""
    # Handle Blender's argument passing: blender -- script.py arg1 arg2
    args = sys.argv
    if '--' in args:
        # Running through Blender
        separator_idx = args.index('--')
        args = args[separator_idx + 1:]
    else:
        # Normal Python execution
        args = args[1:]
    
    # Parse arguments
    if len(args) < 2:
        print("ERROR: Missing required arguments\n", file=sys.stderr)
        print_usage()
        sys.exit(1)
    
    input_file = args[0]
    output_file = args[1]
    
    # Enable debug logging if requested
    if '--debug' in args or '-v' in args:
        logging.getLogger().setLevel(logging.DEBUG)
        logger.debug("Debug logging enabled")
    
    # Load and convert scene to binary format
    success = load_scene(input_file, output_file)
    
    if not success:
        sys.exit(1)
    
    sys.exit(0)


if __name__ == "__main__":
    main()
