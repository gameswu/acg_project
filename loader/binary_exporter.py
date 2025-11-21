"""
Binary Scene Exporter - 高性能二进制格式
相比JSON减少90%文件大小和解析时间
"""

import struct
from pathlib import Path
from typing import BinaryIO
from data_structures import SceneData, Mesh, Material, Vertex


class BinarySceneExporter:
    """导出为自定义二进制格式 (.acg)"""
    
    # 文件魔数和版本
    MAGIC = b'ACGS'  # ACG Scene
    VERSION = 1
    
    def export(self, scene: SceneData, output_path: str):
        """导出场景到二进制文件"""
        with open(output_path, 'wb') as f:
            self._write_header(f)
            self._write_materials(f, scene.materials)
            self._write_textures(f, scene.textures)
            self._write_meshes(f, scene.meshes)
    
    def _write_header(self, f: BinaryIO):
        """写入文件头：魔数(4字节) + 版本(4字节)"""
        f.write(self.MAGIC)
        f.write(struct.pack('I', self.VERSION))
    
    def _write_materials(self, f: BinaryIO, materials: list):
        """写入材质数据"""
        f.write(struct.pack('I', len(materials)))  # 材质数量
        
        for mat in materials:
            # 材质名称（长度 + UTF-8字符串）
            name_bytes = mat.name.encode('utf-8')
            f.write(struct.pack('I', len(name_bytes)))
            f.write(name_bytes)
            
            # PBR参数（紧凑二进制）
            f.write(struct.pack('3f', *mat.base_color))      # 12字节
            f.write(struct.pack('3f', *mat.emission))        # 12字节
            f.write(struct.pack('f', mat.metallic))          # 4字节
            f.write(struct.pack('f', mat.roughness))         # 4字节
            f.write(struct.pack('f', mat.ior))               # 4字节
            f.write(struct.pack('f', mat.opacity))           # 4字节
            
            # 纹理索引（4个int，-1表示无纹理）
            # 确保转换为整数，处理None和其他类型
            def to_texture_index(val):
                if val is None or val == -1:
                    return -1
                return int(val) if isinstance(val, (int, float)) else -1
            
            f.write(struct.pack('4i', 
                to_texture_index(mat.base_color_texture),
                to_texture_index(mat.normal_texture),
                to_texture_index(mat.metallic_roughness_texture),
                to_texture_index(mat.emission_texture)
            ))
            
            # 材质层标志
            flags = 0
            if mat.transmission:
                flags |= 0x01
            if mat.clearcoat:
                flags |= 0x02
            if mat.sheen:
                flags |= 0x04
            f.write(struct.pack('I', flags))
            
            # 扩展层数据（如果有）
            if mat.transmission:
                t = mat.transmission
                f.write(struct.pack('2f', t.strength, mat.ior))
    
    def _write_textures(self, f: BinaryIO, textures: list):
        """写入纹理路径"""
        f.write(struct.pack('I', len(textures)))
        for texture in textures:
            # Handle both Texture objects and string paths
            if hasattr(texture, 'path'):
                tex_path = texture.path
            else:
                tex_path = str(texture)
            path_bytes = tex_path.encode('utf-8')
            f.write(struct.pack('I', len(path_bytes)))
            f.write(path_bytes)
    
    def _write_meshes(self, f: BinaryIO, meshes: list):
        """写入网格数据"""
        f.write(struct.pack('I', len(meshes)))  # 网格数量
        
        for mesh in meshes:
            # 网格名称
            name_bytes = mesh.name.encode('utf-8')
            f.write(struct.pack('I', len(name_bytes)))
            f.write(name_bytes)
            
            # 材质索引
            f.write(struct.pack('I', mesh.material_index))
            
            # 顶点数据（紧凑存储）
            f.write(struct.pack('I', len(mesh.vertices)))
            for v in mesh.vertices:
                # Position (3 floats)
                f.write(struct.pack('3f', *v.position))
                # Normal (3 floats)
                f.write(struct.pack('3f', *v.normal))
                # TexCoord (2 floats)
                f.write(struct.pack('2f', *v.texcoord))
                # Tangent (3 floats)
                f.write(struct.pack('3f', *v.tangent))
                # 每个顶点44字节
            
            # 索引数据
            f.write(struct.pack('I', len(mesh.indices)))
            f.write(struct.pack(f'{len(mesh.indices)}I', *mesh.indices))


class BinarySceneImporter:
    """C++端对应的导入器示例（Python参考实现）"""
    
    def load(self, file_path: str) -> SceneData:
        """从二进制文件加载场景"""
        scene = SceneData()
        
        with open(file_path, 'rb') as f:
            # 验证魔数和版本
            magic = f.read(4)
            if magic != BinarySceneExporter.MAGIC:
                raise ValueError(f"Invalid file format: {magic}")
            
            version = struct.unpack('I', f.read(4))[0]
            if version != BinarySceneExporter.VERSION:
                raise ValueError(f"Unsupported version: {version}")
            
            # 读取数据
            scene.materials = self._read_materials(f)
            scene.textures = self._read_textures(f)
            scene.meshes = self._read_meshes(f)
        
        return scene
    
    def _read_materials(self, f: BinaryIO) -> list:
        """读取材质"""
        count = struct.unpack('I', f.read(4))[0]
        materials = []
        
        for _ in range(count):
            mat = Material()
            
            # 读取名称
            name_len = struct.unpack('I', f.read(4))[0]
            mat.name = f.read(name_len).decode('utf-8')
            
            # PBR参数
            mat.base_color = struct.unpack('3f', f.read(12))
            mat.emission = struct.unpack('3f', f.read(12))
            mat.metallic = struct.unpack('f', f.read(4))[0]
            mat.roughness = struct.unpack('f', f.read(4))[0]
            mat.ior = struct.unpack('f', f.read(4))[0]
            mat.opacity = struct.unpack('f', f.read(4))[0]
            
            # 纹理索引
            tex_indices = struct.unpack('4i', f.read(16))
            mat.base_color_texture = tex_indices[0] if tex_indices[0] >= 0 else None
            
            # 标志
            flags = struct.unpack('I', f.read(4))[0]
            # 读取扩展层...
            
            materials.append(mat)
        
        return materials
    
    def _read_textures(self, f: BinaryIO) -> list:
        """读取纹理列表"""
        count = struct.unpack('I', f.read(4))[0]
        textures = []
        
        for _ in range(count):
            path_len = struct.unpack('I', f.read(4))[0]
            path = f.read(path_len).decode('utf-8')
            textures.append(path)
        
        return textures
    
    def _read_meshes(self, f: BinaryIO) -> list:
        """读取网格"""
        count = struct.unpack('I', f.read(4))[0]
        meshes = []
        
        for _ in range(count):
            mesh = Mesh()
            
            # 名称
            name_len = struct.unpack('I', f.read(4))[0]
            mesh.name = f.read(name_len).decode('utf-8')
            
            # 材质索引
            mesh.material_index = struct.unpack('I', f.read(4))[0]
            
            # 顶点数据
            vert_count = struct.unpack('I', f.read(4))[0]
            for _ in range(vert_count):
                position = struct.unpack('3f', f.read(12))
                normal = struct.unpack('3f', f.read(12))
                texcoord = struct.unpack('2f', f.read(8))
                tangent = struct.unpack('3f', f.read(12))
                
                mesh.vertices.append(Vertex(
                    position=list(position),
                    normal=list(normal),
                    texcoord=list(texcoord),
                    tangent=list(tangent)
                ))
            
            # 索引
            idx_count = struct.unpack('I', f.read(4))[0]
            mesh.indices = list(struct.unpack(f'{idx_count}I', f.read(idx_count * 4)))
            
            meshes.append(mesh)
        
        return meshes
