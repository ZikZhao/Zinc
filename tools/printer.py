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


class ObjectPrinter:
    """Pretty printer for Object pointers - displays repr() and allows expansion"""
    
    def __init__(self, val):
        self.val = val
    
    def _validate_vptr(self, ptr_addr):
        """
        Validate that the vptr at ptr_addr points to a legitimate vtable.
        Returns True if valid, False otherwise.
        
        This prevents Inferior crashes when dereferencing uninitialized pointers
        by checking the vptr on the GDB host side before calling any virtual methods.
        """
        try:
            # Read the first 8 bytes (vptr on 64-bit, adjust if needed)
            import struct
            inferior = gdb.selected_inferior()
            vptr_bytes = inferior.read_memory(ptr_addr, 8)
            vptr_addr = struct.unpack('<Q', vptr_bytes)[0]  # Little-endian 64-bit
            
            # Check if vptr is a reasonable address (not null, not obviously invalid)
            if vptr_addr == 0 or vptr_addr < 0x1000:
                return False
            
            # Query symbol information for the vptr address
            try:
                symbol_info = gdb.execute(f"info symbol 0x{vptr_addr:x}", to_string=True)
                
                # Valid vtable symbols contain "vtable for " (demangled) or start with "_ZTV" (mangled)
                if "vtable for " in symbol_info or "_ZTV" in symbol_info:
                    return True
                
                # If we got a symbol but it's not a vtable, it's invalid
                return False
            except gdb.error:
                # No symbol found at this address
                return False
                
        except gdb.MemoryError:
            # Cannot read memory at ptr_addr (unmapped)
            return False
        except Exception:
            # Any other error (struct unpack failure, etc.)
            return False
    
    def _is_safe_to_evaluate(self):
        """
        Check if it's safe to execute Inferior calls by inspecting the call stack.
        Returns False if the inferior is in signal handling or ASan error reporting,
        which could corrupt the crash context.
        """
        try:
            frame = gdb.newest_frame()
            if not frame:
                return False
            
            # Check up to 10 frames for dangerous function patterns
            dangerous_patterns = [
                'asan',          # ASan runtime error pipeline
                'abort',         # POSIX abort()
                'raise',         # POSIX raise()
                'terminate',     # C++ std::terminate
                '__asan',        # ASan internal functions
                '__sanitizer'    # Sanitizer runtime
            ]
            
            for i in range(10):
                if not frame:
                    break
                
                try:
                    frame_name = frame.name()
                    if frame_name:
                        frame_name_lower = frame_name.lower()
                        for pattern in dangerous_patterns:
                            if pattern in frame_name_lower:
                                # Found dangerous pattern - unsafe to evaluate
                                return False
                    
                    # Move to older frame
                    frame = frame.older()
                except Exception:
                    # DWARF parsing failure, register corruption, or other unwind errors
                    # Take conservative approach: assume unsafe
                    return False
            
            # No dangerous patterns found
            return True
            
        except Exception:
            # Any error during stack inspection - assume unsafe
            return False
    
    def to_string(self):
        # Dereference the pointer if needed
        if self.val.type.code == gdb.TYPE_CODE_PTR:
            ptr_addr = int(self.val)
            if ptr_addr == 0:
                return "nullptr"
        else:
            try:
                ptr_addr = int(self.val.address)
            except:
                return "<no address>"
        
        # Check if inferior is running
        if not gdb.selected_inferior().threads():
            return f"(0x{ptr_addr:x}) <no inferior running>"
        
        # Validate vptr before attempting to call virtual methods
        if not self._validate_vptr(ptr_addr):
            return f"(0x{ptr_addr:x}) <uninitialized/invalid vptr>"
        
        # Check if we're in a crash/signal handling state before making inferior calls
        if not self._is_safe_to_evaluate():
            return f"(0x{ptr_addr:x}) <process crashing, repr() disabled>"
        
        # vptr is valid and stack is clean, safe to call repr()
        repr_result = gdb.parse_and_eval(f"((Object*)0x{ptr_addr:x})->repr()")
        
        # repr() returns std::string_view with _M_str and _M_len
        str_ptr = repr_result['_M_str']
        str_len = int(repr_result['_M_len'])
        
        if str_len > 0 and str_ptr:
            repr_str = str_ptr.string(length=str_len)
            return f"(0x{ptr_addr:x}) {repr_str}"
        else:
            return f'(0x{ptr_addr:x}) ""'
    
    def children(self):
        """Allow expanding to see the actual object members"""
        if self.val.type.code == gdb.TYPE_CODE_PTR:
            ptr_addr = int(self.val)
            if ptr_addr == 0:
                return
            
            # Validate vptr before attempting to expand children
            if not self._validate_vptr(ptr_addr):
                yield ('<error>', 'uninitialized/invalid vptr')
                return
            
            obj = self.val.dereference()
        else:
            obj = self.val
        
        # Yield all fields of the object
        obj_type = obj.type
        for field in obj_type.fields():
            if hasattr(field, 'name') and field.name:
                field_value = obj[field.name]
                yield (field.name, field_value)
    
    def display_hint(self):
        return None


def object_printer_lookup(val):
    """Lookup function to check if a value should use ObjectPrinter"""
    try:
        if val.type.code != gdb.TYPE_CODE_PTR:
            return None
        
        # Get the pointed-to type name
        pointed_type = val.type.target()
        type_name = str(pointed_type.unqualified().strip_typedefs())
        
        # Check if this is an Object-derived type
        # List all known Object-derived types
        object_types = {
            'Object', 'Type', 'Value',
            'UnknownType', 'AnyType', 'NullptrType', 'IntegerType', 'FloatType', 
            'BooleanType', 'FunctionType', 'ArrayType', 'StructType', 
            'InterfaceType', 'InstanceType', 'MutableType', 'ReferenceType', 
            'PointerType', 'IntersectionType', 'UnionType',
            'UnknownValue', 'NullptrValue', 'IntegerValue', 'FloatValue', 
            'BooleanValue', 'FunctionValue', 'ArrayValue', 'StructValue', 
            'InterfaceValue', 'InstanceValue', 'MutValue', 'ReferenceValue', 
            'PointerValue', 'FunctionOverloadSetValue'
        }
        
        if type_name in object_types:
            return ObjectPrinter(val)
    except gdb.MemoryError:
        # Can't access memory to determine type
        pass
    except Exception as e:
        # Debug: log lookup failures (but suppress to avoid console spam)
        # gdb.write(f"object_printer_lookup exception: {e}\n")
        # gdb.flush()
        pass
    
    return None


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


# Register the pretty printer for standard types
gdb.printing.register_pretty_printer(gdb.current_objfile(), build_pretty_printer())

# Register Object printer separately using lookup function  
gdb.pretty_printers.append(object_printer_lookup)

gdb.write("Zinc pretty printers loaded successfully!\n")
gdb.write("Registered printers for:\n")
gdb.write("  - TypeResolution\n")
gdb.write("  - GlobalMemory::FlatSet\n")
gdb.write("  - GlobalMemory::FlatMap\n")
gdb.write("  - GlobalMemory::MultiMap\n")
gdb.write("  - ComparableSpan\n")
gdb.write("  - Object/Type/Value pointers (with repr() via lookup function)\n")
gdb.write(f"Total global pretty printers: {len(gdb.pretty_printers)}\n")
gdb.flush()


# Helper function to test the printer manually
class TestObjectPrinter(gdb.Command):
    """Test Object printer: test-object-printer <expression>"""
    
    def __init__(self):
        super(TestObjectPrinter, self).__init__("test-object-printer", gdb.COMMAND_USER)
    
    def invoke(self, arg, from_tty):
        try:
            val = gdb.parse_and_eval(arg)
            gdb.write(f"Value type: {val.type}\n")
            gdb.write(f"Is pointer: {val.type.code == gdb.TYPE_CODE_PTR}\n")
            
            if val.type.code == gdb.TYPE_CODE_PTR:
                ptr_addr = int(val)
                gdb.write(f"Pointer address: 0x{ptr_addr:x}\n")
                
                try:
                    pointed = val.type.target()
                    gdb.write(f"Points to: {pointed}\n")
                    gdb.write(f"Unqualified: {pointed.unqualified()}\n")
                    gdb.write(f"Stripped: {pointed.unqualified().strip_typedefs()}\n")
                except Exception as e:
                    gdb.write(f"Cannot get pointed type: {e}\n")
                
                # Try to dereference
                try:
                    deref = val.dereference()
                    gdb.write(f"Dereference OK\n")
                except gdb.MemoryError:
                    gdb.write(f"ERROR: Cannot dereference - invalid memory\n")
                except Exception as e:
                    gdb.write(f"ERROR: Cannot dereference - {e}\n")
            
            printer = object_printer_lookup(val)
            if printer:
                gdb.write(f"Printer found: {printer}\n")
                try:
                    result = printer.to_string()
                    gdb.write(f"Result: {result}\n")
                except Exception as e:
                    gdb.write(f"ERROR calling to_string(): {e}\n")
                    import traceback
                    gdb.write(traceback.format_exc())
            else:
                gdb.write("No printer found\n")
        except gdb.MemoryError as e:
            gdb.write(f"Memory Error: {e}\n")
        except Exception as e:
            gdb.write(f"Error: {e}\n")
            import traceback
            gdb.write(traceback.format_exc())
        gdb.flush()

TestObjectPrinter()
