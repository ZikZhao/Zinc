"""
GDB Pretty Printers for Zinc Data Structures
TypeResolution, FlatSet, FlatMap
"""

import gdb
import re


class TypeResolutionPrinter:
    """Pretty printer for TypeResolution class"""
    
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        ptr_raw = int(self.val['ptr_'])
        flag_bit = ptr_raw & 1
        actual_ptr = ptr_raw & ~1
        
        if actual_ptr == 0:
            return "TypeResolution { (null) 0x0 }"
        
        is_sized = (flag_bit == 0)
        status = "sized" if is_sized else "unsized"
        
        return f"TypeResolution {{ ({status}) 0x{actual_ptr:x} }}"
    
    def children(self):
        ptr_raw = int(self.val['ptr_'])
        flag_bit = ptr_raw & 1
        actual_ptr = ptr_raw & ~1
        is_sized = (flag_bit == 0)
        
        # Create proper gdb.Value objects
        ptr_type = gdb.lookup_type('Type').pointer()
        ptr_value = gdb.Value(actual_ptr).cast(ptr_type)
        
        # Create a boolean value
        bool_type = gdb.lookup_type('bool')
        sized_value = gdb.Value(1 if is_sized else 0).cast(bool_type)
        
        yield ('ptr_', ptr_value)
        yield ('is_sized', sized_value)
    
    def display_hint(self):
        return None


class FlatSetPrinter:
    """Pretty printer for GlobalMemory::FlatSet"""
    
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        try:
            keys = self.val['keys_']
            size = int(keys['_M_impl']['_M_finish'] - keys['_M_impl']['_M_start'])
            return f"FlatSet with {size} elements"
        except:
            return "FlatSet"
    
    def children(self):
        try:
            keys = self.val['keys_']
            start = keys['_M_impl']['_M_start']
            finish = keys['_M_impl']['_M_finish']
            size = int(finish - start)
            
            for i in range(min(size, 100)):  # Limit to 100 elements
                yield (f'[{i}]', start[i])
        except Exception as e:
            yield ('error', str(e))
    
    def display_hint(self):
        return 'array'


class FlatMapPrinter:
    """Pretty printer for GlobalMemory::FlatMap"""
    
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        try:
            keys = self.val['keys_']
            size = int(keys['_M_impl']['_M_finish'] - keys['_M_impl']['_M_start'])
            return f"FlatMap with {size} entries"
        except:
            return "FlatMap"
    
    def children(self):
        try:
            keys = self.val['keys_']
            values = self.val['values_']
            
            keys_start = keys['_M_impl']['_M_start']
            keys_finish = keys['_M_impl']['_M_finish']
            values_start = values['_M_impl']['_M_start']
            
            size = int(keys_finish - keys_start)
            
            for i in range(min(size, 100)):  # Limit to 100 elements
                key = keys_start[i]
                value = values_start[i]
                # Yield key-value pairs
                yield (f'[{i}].key', key)
                yield (f'[{i}].value', value)
        except Exception as e:
            yield ('error', str(e))
    
    def display_hint(self):
        return 'map'


class MultiMapPrinter:
    """Pretty printer for GlobalMemory::MultiMap"""
    
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        try:
            keys = self.val['keys_']
            size = int(keys['_M_impl']['_M_finish'] - keys['_M_impl']['_M_start'])
            return f"MultiMap with {size} entries"
        except:
            return "MultiMap"
    
    def children(self):
        try:
            keys = self.val['keys_']
            values = self.val['values_']
            
            keys_start = keys['_M_impl']['_M_start']
            keys_finish = keys['_M_impl']['_M_finish']
            values_start = values['_M_impl']['_M_start']
            
            size = int(keys_finish - keys_start)
            
            for i in range(min(size, 100)):
                key = keys_start[i]
                value = values_start[i]
                yield (f'[{i}].key', key)
                yield (f'[{i}].value', value)
        except Exception as e:
            yield ('error', str(e))
    
    def display_hint(self):
        return 'map'


class ComparableSpanPrinter:
    """Pretty printer for ComparableSpan"""
    
    def __init__(self, val):
        self.val = val
    
    def to_string(self):
        # ComparableSpan inherits from std::span
        try:
            size = int(self.val['_M_extent']['_M_extent_value'])
        except:
            # Try alternative member access
            try:
                size = int(self.val['_M_extent'])
            except:
                size = 0
        
        return f"ComparableSpan with {size} elements"
    
    def children(self):
        try:
            ptr = self.val['_M_ptr']
            try:
                size = int(self.val['_M_extent']['_M_extent_value'])
            except:
                size = int(self.val['_M_extent'])
            
            for i in range(min(size, 100)):
                yield (f'[{i}]', ptr[i])
        except:
            pass
    
    def display_hint(self):
        return 'array'


def build_pretty_printer():
    """Build and register the pretty printer"""
    pp = gdb.printing.RegexpCollectionPrettyPrinter("Zinc")
    
    # TypeResolution
    pp.add_printer('TypeResolution', '^TypeResolution$', TypeResolutionPrinter)
    
    # FlatSet
    pp.add_printer('FlatSet', '^GlobalMemory::FlatSet<.*>$', FlatSetPrinter)
    
    # FlatMap
    pp.add_printer('FlatMap', '^GlobalMemory::FlatMap<.*>$', FlatMapPrinter)
    
    # MultiMap
    pp.add_printer('MultiMap', '^GlobalMemory::MultiMap<.*>$', MultiMapPrinter)
    
    # ComparableSpan
    pp.add_printer('ComparableSpan', '^ComparableSpan<.*>$', ComparableSpanPrinter)
    
    return pp


# Register the pretty printer
gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer())

print("Zinc pretty printers loaded successfully!")
print("Registered printers for:")
print("  - TypeResolution")
print("  - GlobalMemory::FlatSet")
print("  - GlobalMemory::FlatMap")
print("  - GlobalMemory::MultiMap")
print("  - ComparableSpan")
