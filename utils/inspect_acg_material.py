"""
Inspect ACG binary file material data
"""
import struct
import sys

def read_string(f):
    length = struct.unpack('I', f.read(4))[0]
    return f.read(length).decode('utf-8')

def inspect_acg(filepath):
    with open(filepath, 'rb') as f:
        # Header
        magic = f.read(4)
        version = struct.unpack('I', f.read(4))[0]
        print(f"Magic: {magic}, Version: {version}")
        
        # Materials count
        mat_count = struct.unpack('I', f.read(4))[0]
        print(f"\nMaterials: {mat_count}")
        
        for i in range(mat_count):
            name = read_string(f)
            print(f"\n[Material {i}] {name}")
            
            # PBR parameters
            base_color = struct.unpack('3f', f.read(12))
            emission = struct.unpack('3f', f.read(12))
            metallic, roughness, ior, opacity = struct.unpack('4f', f.read(16))
            
            print(f"  Base Color: {base_color}")
            print(f"  Emission: {emission}")
            print(f"  Metallic: {metallic:.3f}")
            print(f"  Roughness: {roughness:.3f}")
            print(f"  IOR: {ior:.3f}")
            print(f"  Opacity: {opacity:.3f}")
            
            # Texture indices (5 textures now: base, normal, mr, emission, opacity)
            tex_indices = struct.unpack('5i', f.read(20))
            print(f"  Textures: base={tex_indices[0]}, normal={tex_indices[1]}, mr={tex_indices[2]}, emission={tex_indices[3]}, opacity={tex_indices[4]}")
            
            # Layer flags
            flags = struct.unpack('I', f.read(4))[0]
            print(f"  Layer Flags: 0x{flags:02X}")
            
            layer_names = []
            if flags & 0x01: layer_names.append("Clearcoat")
            if flags & 0x02: layer_names.append("Transmission")
            if flags & 0x04: layer_names.append("Sheen")
            if flags & 0x08: layer_names.append("Subsurface")
            if flags & 0x10: layer_names.append("Anisotropy")
            if flags & 0x20: layer_names.append("Iridescence")
            if flags & 0x40: layer_names.append("Volume")
            
            print(f"  Active Layers: {', '.join(layer_names) if layer_names else 'None'}")
            
            # Read layer data
            for layer_name in layer_names:
                print(f"  [{layer_name} Layer - 32 bytes]")
                if layer_name == "Subsurface":
                    color = struct.unpack('3f', f.read(12))
                    radius = struct.unpack('f', f.read(4))[0]
                    radius_scale = struct.unpack('3f', f.read(12))
                    strength = struct.unpack('f', f.read(4))[0]
                    print(f"    Color: {color}")
                    print(f"    Radius: {radius:.3f}")
                    print(f"    Radius Scale: {radius_scale}")
                    print(f"    Strength (Weight): {strength:.3f}")
                else:
                    # Skip other layer data for now
                    f.read(32)

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python inspect_acg_material.py <acg_file>")
        sys.exit(1)
    
    inspect_acg(sys.argv[1])
