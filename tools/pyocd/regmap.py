from types import MappingProxyType
from typing import Collection, Iterable, List, Mapping, MutableMapping, Sequence
import typing
from pyocd.debug.svd.model import SVDDevice, SVDRegister, SVDField, SVDPeripheral
from pyocd.core.target import Target


class ContainerBase(Iterable):
    __slots__ = "_path"

    # Factory function to create new container types
    # meant to be called internally in the child class.
    @staticmethod
    def _factory(cls, name: str, fields: Collection[str], path: Sequence[str]):
        # Prevent modification of slots before instantiation by the caller
        slots = tuple(fields)
        # Prevent modification of class definition by the caller
        classdef = {"__slots__": slots}
        subcls = type(".".join(path) + name, (cls,), classdef)
        subcls._path = path
        return subcls

    def __new__(cls):
        raise NotImplementedError()

    # Functions to be overridden in child classes
    def _attr_factory(self, attr_name: str):
        raise NotImplementedError()

    def __getattr__(self, attr):
        if attr in self.__slots__:
            new_attr = self._attr_factory(attr)
            object.__setattr__(self, attr, new_attr)
            return new_attr
        else:
            raise AttributeError()

    def __setattr__(self, name, value):
        # For managed attributes,
        # create the attr (if needed) by calling __getattr__
        # then call the object's __set__ handler
        if name in self.__slots__:
            attr = self.__getattr__(name)
            attr.__set__(self, value)
        # Otherwise, use default implementation
        else:
            object.__setattr__(self, name, value)

    def __dir__(self):
        return self.__slots__

    def __len__(self):
        return len(self.__slots__)

    def __iter__(self):
        for attr in self.__slots__:
            yield getattr(self, attr)

    def __repr__(self):
        lines = []
        desc = str(self) + ":"
        lines.append(desc)
        prefix = " " * len(self._path[-1])
        for child_obj in self:
            lines.append(prefix + child_obj.__str__())

        return "\n".join(lines)

    def __getitem__(self, x):
        if type(x) == str and x in self.__slots__:
            return self.__getattr__(x)
        else:
            raise KeyError()


# Device contains many Peripheral
class Device(ContainerBase):
    __slots__ = "_target", "_svd_device", "_svd_peripheral_map"

    @staticmethod
    def _factory(cls, target: Target, svd_device: SVDDevice):
        svd_peripheral_map: MutableMapping[str, SVDPeripheral] = dict()

        for peripheral in svd_device.peripherals:  # type: SVDPeripheral
            svd_peripheral_map[peripheral.name] = peripheral

        subcls = ContainerBase._factory(
            cls,
            svd_device.name + "Device",
            svd_peripheral_map.keys(),
            (svd_device.name,),
        )
        subcls._target = target
        subcls._svd_device = svd_device
        subcls._svd_peripheral_map = svd_peripheral_map
        return subcls

    def __new__(cls, target: Target, svd_device: SVDDevice):
        return object.__new__(cls._factory(cls, target, svd_device))

    def _attr_factory(self, attr_name: str):
        return Peripheral(self._target, self._svd_peripheral_map[attr_name])

    def __repr__(self):
        return self._path[0]

    def __str__(self):
        return self._path[0]


# Peripheral contains many Register objects
class Peripheral(ContainerBase):
    __slots__ = "_target", "_svd_peripheral", "_svd_register_map", "_base_address"

    @staticmethod
    def _factory(cls, target: Target, svd_peripheral: SVDPeripheral):
        svd_register_map: MutableMapping[str, SVDRegister] = dict()

        for reg in svd_peripheral.registers:  # type: SVDRegister
            svd_register_map[reg.name] = reg

        subcls = ContainerBase._factory(
            cls,
            svd_peripheral.name + "Peripheral",
            svd_register_map.keys(),
            (svd_peripheral.parent.name, svd_peripheral.name),
        )

        subcls._target = target
        subcls._svd_peripheral = svd_peripheral
        subcls._svd_register_map = svd_register_map
        subcls._base_address = svd_peripheral.base_address
        return subcls

    def __new__(cls, target: Target, svd_peripheral: SVDPeripheral):
        return object.__new__(cls._factory(cls, target, svd_peripheral))

    def _attr_factory(self, attr_name: str):
        return Register(self._target, self._svd_register_map[attr_name])

    def __str__(self):
        return "{}[0x{:X}]".format(self._path[-1], self._base_address)


# Base class for Field and Register objects
class RegFieldBase(ContainerBase):
    __slots__ = "_lvalue", "_width_bits", "_value_fmt"

    @staticmethod
    def _factory(cls, ct_name, path, lvalue, fields, width_bits):
        subcls = ContainerBase._factory(cls, ct_name, fields, path)
        subcls._lvalue = lvalue
        subcls._width_bits = width_bits
        subcls._value_fmt = "0x{{:0{}X}}".format(RegFieldBase._width_bytes(width_bits))
        return subcls

    def _width_bytes(width_bits):
        return (width_bits + 3) // 4

    def __str__(self):
        assert self._value_fmt
        return ("{}=" + self._value_fmt).format(self._path[-1], self.read())

    def __set__(self, instance, value):
        assert self.write
        return self.write(value)

    def __bytes__(self):
        assert self.read
        assert self._width_bits
        return self.to_bytes(
            self._width_bytes(self._width_bits), byteorder="little", signed=False
        )

    # Support subscripting and slicing
    def __getitem__(self, key):
        if isinstance(key, str):
            return super().__getitem__(key)
        elif isinstance(key, slice):
            if key.step and key.step != 1:
                raise KeyError()
            elif key.start > key.stop:
                raise KeyError()
            else:
                mask = (2 ** (key.stop - key.start)) - 1
                return (self.read() >> key.start) & mask
        elif isinstance(key, int):
            mask = 0b1
            return (self.read() >> key) & mask
        else:
            raise KeyError()

    # Support conversion to int
    def __index__(self):
        assert self.read
        return self.read()


class Register(RegFieldBase):
    __slots__ = "_target", "_addr", "_default_mask", "_svd_register"

    @staticmethod
    def _factory(cls, target: Target, svd_register: SVDRegister):
        fields: List[str] = list()

        field: SVDRegister
        for field in svd_register.fields:  # type: SVDRegister
            fields.append(field.name)

        ct_name = svd_register.name + "Register"
        svd_periph: SVDPeripheral = svd_register.parent
        svd_device: SVDDevice = svd_periph.parent

        reg_addr = svd_periph.base_address + svd_register.address_offset
        addr_str = "0x{{:0{}X}}".format(cls._width_bytes(svd_register.size)).format(
            reg_addr
        )

        path: Sequence[str] = (svd_device.name, svd_periph.name, svd_register.name)
        lvalue = "{}[{}]".format(".".join(path).upper(), addr_str)

        subcls = RegFieldBase._factory(
            cls, ct_name, path, lvalue, fields, svd_register.size
        )
        subcls._target = target
        subcls._addr = reg_addr
        subcls._default_mask = (2 ** svd_register.size) - 1
        subcls._svd_register = svd_register
        return subcls

    def __new__(cls, target: Target, svd_register: SVDRegister):
        subcls = cls._factory(cls, target, svd_register)
        return object.__new__(subcls)

    def read(self, read_mask=None):
        if not read_mask:
            read_mask = self._default_mask

        cur_val = self._target.read_memory(self._addr, transfer_size=self._width_bits)

        return cur_val & read_mask

    def write(self, value, write_mask=None):
        # Handle partial writes (read reg first)
        if write_mask and write_mask != self._default_mask:
            # Invert the write mask to determine the read mask
            read_mask = 0xFFFFFFFF ^ write_mask
            value = (self.read() & read_mask) | (value & write_mask)

        return self._target.write_memory(
            self._addr, value, transfer_size=self._width_bits
        )

    def _attr_factory(self, attr_name: str):
        svd_field: SVDField = None
        for field in self._svd_register.fields:
            if field.name == attr_name:
                svd_field = field
                break

        if svd_field:
            return Field(self, svd_field)

        return None

    def __str__(self):
        # return self._value_fmt.format(self.read())
        return "{}[0x{:X}]={}".format(
            self._path[-1], self._addr, self._value_fmt.format(self.read())
        )


class Field(RegFieldBase):
    __slots__ = "_reg", "_offset", "_rw_mask"

    @staticmethod
    def _factory(cls, reg, svd_field: SVDField):
        name = svd_field.name + "Field"

        svd_register: SVDRegister = svd_field.parent
        svd_periph: SVDPeripheral = svd_register.parent
        svd_device: SVDDevice = svd_periph.parent

        path = (svd_device.name, svd_periph.name, svd_register.name, svd_field.name)
        lvalue = ".".join(path[-3:])

        subcls = RegFieldBase._factory(cls, name, path, lvalue, (), svd_field.bit_width)

        subcls._reg = reg
        subcls._offset = svd_field.bit_offset
        subcls._rw_mask = ((2 ** svd_field.bit_width) - 1) << svd_field.bit_offset
        return subcls

    def __new__(cls, reg: typing.Type[Register], svd_field: SVDField):
        subcls = cls._factory(cls, reg, svd_field)
        return object.__new__(subcls)

    def read(self):
        mask = self._rw_mask
        offset = self._offset
        reg = self._reg
        read = reg.read
        regval = read(mask) >> offset

        return regval

    def write(self, value):
        return self._reg.write(value << self._offset, self._rw_mask)

    def __repr__(self):
        return self._path[-2] + self.__str__()

    def __str__(self):
        lvalue = ""
        if self._width_bits == 1:
            lvalue = "[{}]:{}".format(self._offset, self._path[-1])
        else:
            lvalue = "[{}:{}]:{}".format(
                self._offset, self._offset + self._width_bits, self._path[-1]
            )

        return "{}={}".format(lvalue, self._value_fmt.format(self.read()))
