"""
Blender Material Inspector - Debug Tool
用于检查Blender文件中的材质属性和节点连接

使用方法:
    python inspect_blend_material.py <file.blend> [options]

选项:
    --material NAME    只显示指定材质
    --verbose          显示详细的节点信息
    --export JSON      导出材质信息到JSON文件
"""

import sys
import json
import argparse
from pathlib import Path

# Add loader directory to path
script_dir = Path(__file__).parent
loader_dir = script_dir.parent / "loader"
sys.path.insert(0, str(loader_dir))

try:
    import bpy
    HAS_BPY = True
except ImportError:
    HAS_BPY = False
    print("ERROR: bpy module not found. Install with: pip install bpy")
    sys.exit(1)


class BlendMaterialInspector:
    """Blender材质检查器"""
    
    def __init__(self, blend_file: str):
        self.blend_file = Path(blend_file)
        if not self.blend_file.exists():
            raise FileNotFoundError(f"Blend file not found: {blend_file}")
        
        # Load blend file
        bpy.ops.wm.read_factory_settings(use_empty=True)
        bpy.ops.wm.open_mainfile(filepath=str(self.blend_file))
        
    def inspect_all(self, material_filter=None, verbose=False):
        """检查所有材质"""
        print(f"\n{'='*80}")
        print(f"BLENDER MATERIAL INSPECTOR")
        print(f"File: {self.blend_file}")
        print(f"{'='*80}\n")
        
        print(f"Scene Overview:")
        print(f"  Objects: {len(bpy.data.objects)}")
        print(f"  Materials: {len(bpy.data.materials)}")
        print(f"  Images: {len(bpy.data.images)}")
        print()
        
        materials_data = []
        
        for mat_idx, mat in enumerate(bpy.data.materials):
            if material_filter and mat.name != material_filter:
                continue
            
            mat_data = self.inspect_material(mat, mat_idx, verbose)
            materials_data.append(mat_data)
        
        return materials_data
    
    def inspect_material(self, mat, mat_idx, verbose=False):
        """检查单个材质"""
        print(f"\n{'='*80}")
        print(f"MATERIAL [{mat_idx}]: {mat.name}")
        print(f"{'='*80}")
        
        mat_data = {
            'name': mat.name,
            'index': mat_idx,
            'use_nodes': mat.use_nodes if hasattr(mat, 'use_nodes') else False,
            'inputs': {},
            'textures': [],
            'normal_map': None,
            'volume': None
        }
        
        if not mat.use_nodes:
            print("  ✗ Material does not use nodes")
            return mat_data
        
        node_tree = mat.node_tree
        print(f"  Nodes: {len(node_tree.nodes)}")
        
        if verbose:
            print(f"\n  All Nodes:")
            for node in node_tree.nodes:
                print(f"    - {node.type}: {node.name}")
        
        # Find Principled BSDF
        principled = None
        for node in node_tree.nodes:
            if node.type == 'BSDF_PRINCIPLED':
                principled = node
                break
        
        if not principled:
            print("  ✗ No Principled BSDF node found")
            return mat_data
        
        print(f"  ✓ Found Principled BSDF")
        
        # Inspect inputs
        self._inspect_inputs(principled, mat_data, verbose)
        
        # Check volume shader
        self._inspect_volume(node_tree, mat_data)
        
        return mat_data
    
    def _inspect_inputs(self, principled, mat_data, verbose):
        """检查Principled BSDF的输入"""
        print(f"\n  Key Material Properties:")
        print(f"  {'-'*76}")
        
        inputs = principled.inputs
        important_props = [
            'Base Color', 'Metallic', 'Roughness', 'IOR', 'Alpha',
            'Normal', 'Subsurface Weight', 'Subsurface Radius', 
            'Subsurface Scale', 'Subsurface Color',
            'Transmission Weight', 'Coat Weight', 'Sheen Weight',
            'Emission Color', 'Emission Strength'
        ]
        
        for prop_name in important_props:
            if prop_name not in inputs:
                continue
            
            socket = inputs[prop_name]
            info = self._get_socket_info(socket)
            mat_data['inputs'][prop_name] = info
            
            # Format display
            status = "🔗" if info['linked'] else "  "
            value_str = self._format_value(info['value'])
            link_str = f" → {info['link_info']}" if info['linked'] else ""
            
            print(f"    {status} {prop_name:25s} = {value_str:30s}{link_str}")
            
            if info['linked'] and info['texture']:
                mat_data['textures'].append({
                    'property': prop_name,
                    'image': info['texture']['name'],
                    'packed': info['texture']['packed'],
                    'size': info['texture']['size']
                })
        
        if verbose:
            print(f"\n  All Inputs (Verbose):")
            print(f"  {'-'*76}")
            input_names = list(inputs.keys())
            for input_name in input_names:
                if input_name in important_props:
                    continue
                try:
                    socket = inputs[input_name]
                    info = self._get_socket_info(socket)
                    status = "🔗" if info['linked'] else "  "
                    value_str = self._format_value(info['value'])
                    print(f"    {status} {input_name:25s} = {value_str}")
                except:
                    pass
    
    def _get_socket_info(self, socket):
        """获取socket的详细信息"""
        info = {
            'linked': socket.is_linked,
            'value': None,
            'link_info': None,
            'texture': None
        }
        
        # Get value
        try:
            value = socket.default_value
            if hasattr(value, '__len__') and len(value) >= 3:
                info['value'] = [float(value[0]), float(value[1]), float(value[2])]
            else:
                info['value'] = float(value)
        except:
            info['value'] = None
        
        # Get link info
        if socket.is_linked:
            link = socket.links[0]
            from_node = link.from_node
            
            if from_node.type == 'TEX_IMAGE':
                img = from_node.image
                info['link_info'] = f"Image: {img.name if img else 'None'}"
                if img:
                    info['texture'] = {
                        'name': img.name,
                        'packed': img.packed_file is not None,
                        'size': f"{img.size[0]}x{img.size[1]}",
                        'filepath': img.filepath if img.filepath else 'N/A'
                    }
            elif from_node.type == 'NORMAL_MAP':
                # Check normal map's input
                if 'Color' in from_node.inputs and from_node.inputs['Color'].is_linked:
                    tex_link = from_node.inputs['Color'].links[0]
                    tex_node = tex_link.from_node
                    if tex_node.type == 'TEX_IMAGE' and tex_node.image:
                        img = tex_node.image
                        info['link_info'] = f"NormalMap → Image: {img.name}"
                        info['texture'] = {
                            'name': img.name,
                            'packed': img.packed_file is not None,
                            'size': f"{img.size[0]}x{img.size[1]}",
                            'filepath': img.filepath if img.filepath else 'N/A'
                        }
                        # Extract normal map info
                        info['normal_map'] = {
                            'strength': float(from_node.inputs['Strength'].default_value) if 'Strength' in from_node.inputs else 1.0
                        }
                else:
                    info['link_info'] = "NormalMap (no texture)"
            elif from_node.type == 'VALUE':
                info['link_info'] = f"Value: {from_node.outputs[0].default_value:.4f}"
            elif from_node.type == 'RGB':
                rgb = from_node.outputs[0].default_value
                info['link_info'] = f"RGB: ({rgb[0]:.3f}, {rgb[1]:.3f}, {rgb[2]:.3f})"
            elif from_node.type == 'MIX':
                info['link_info'] = f"Mix ({from_node.blend_type if hasattr(from_node, 'blend_type') else 'unknown'})"
            else:
                info['link_info'] = from_node.type
        
        return info
    
    def _format_value(self, value):
        """格式化值用于显示"""
        if value is None:
            return "N/A"
        elif isinstance(value, (list, tuple)) and len(value) >= 3:
            return f"RGB({value[0]:.3f}, {value[1]:.3f}, {value[2]:.3f})"
        elif isinstance(value, float):
            return f"{value:.4f}"
        else:
            return str(value)
    
    def _inspect_volume(self, node_tree, mat_data):
        """检查体积着色器"""
        print(f"\n  Volume Shader:")
        print(f"  {'-'*76}")
        
        output_node = None
        for node in node_tree.nodes:
            if node.type == 'OUTPUT_MATERIAL':
                output_node = node
                break
        
        if not output_node or 'Volume' not in output_node.inputs:
            print(f"    ✗ No volume output socket")
            return
        
        volume_socket = output_node.inputs['Volume']
        if not volume_socket.is_linked:
            print(f"    ✗ No volume shader connected")
            return
        
        volume_node = volume_socket.links[0].from_node
        print(f"    ✓ Volume shader: {volume_node.type}")
        
        mat_data['volume'] = {
            'type': volume_node.type,
            'properties': {}
        }
        
        if volume_node.type == 'VOLUME_PRINCIPLED':
            print(f"      Principled Volume properties:")
            for prop in ['Density', 'Color', 'Anisotropy', 'Absorption Color', 'Emission Strength']:
                if prop in volume_node.inputs:
                    val = volume_node.inputs[prop].default_value
                    if hasattr(val, '__len__'):
                        val_str = f"({val[0]:.3f}, {val[1]:.3f}, {val[2]:.3f})"
                    else:
                        val_str = f"{float(val):.4f}"
                    print(f"        {prop}: {val_str}")
                    mat_data['volume']['properties'][prop] = val_str
    
    def list_images(self):
        """列出所有图像"""
        print(f"\n{'='*80}")
        print(f"IMAGE/TEXTURE INVENTORY")
        print(f"{'='*80}")
        
        images_data = []
        
        for img_idx, img in enumerate(bpy.data.images):
            print(f"\n  [{img_idx}] {img.name}")
            print(f"      Size: {img.size[0]}x{img.size[1]}")
            print(f"      Channels: {img.channels}")
            print(f"      Packed: {'YES ✓' if img.packed_file else 'NO'}")
            print(f"      Format: {img.file_format}")
            print(f"      Filepath: {img.filepath if img.filepath else 'N/A'}")
            
            images_data.append({
                'name': img.name,
                'size': [img.size[0], img.size[1]],
                'channels': img.channels,
                'packed': img.packed_file is not None,
                'format': img.file_format,
                'filepath': img.filepath if img.filepath else None
            })
        
        return images_data


def main():
    parser = argparse.ArgumentParser(
        description='Inspect Blender material properties for debugging',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  python inspect_blend_material.py scene.blend
  python inspect_blend_material.py scene.blend --material "Cloud"
  python inspect_blend_material.py scene.blend --verbose
  python inspect_blend_material.py scene.blend --export materials.json
        """
    )
    
    parser.add_argument('blend_file', help='Path to .blend file')
    parser.add_argument('--material', '-m', help='Filter by material name')
    parser.add_argument('--verbose', '-v', action='store_true', help='Show verbose output')
    parser.add_argument('--export', '-e', help='Export to JSON file')
    
    args = parser.parse_args()
    
    try:
        inspector = BlendMaterialInspector(args.blend_file)
        
        # Inspect materials
        materials_data = inspector.inspect_all(
            material_filter=args.material,
            verbose=args.verbose
        )
        
        # List images
        images_data = inspector.list_images()
        
        # Export if requested
        if args.export:
            export_data = {
                'file': str(inspector.blend_file),
                'materials': materials_data,
                'images': images_data
            }
            
            export_path = Path(args.export)
            with open(export_path, 'w', encoding='utf-8') as f:
                json.dump(export_data, f, indent=2, ensure_ascii=False)
            
            print(f"\n✓ Exported material data to: {export_path}")
        
        print(f"\n{'='*80}")
        print(f"INSPECTION COMPLETE")
        print(f"{'='*80}\n")
        
    except Exception as e:
        print(f"\n✗ Error: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())
