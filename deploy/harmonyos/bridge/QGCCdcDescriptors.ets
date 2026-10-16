import { UsbSerialDriverError } from './QGCUsbSerialTypes';

export interface CdcInterfacePair {
  controlId: number;
  dataId: number;
  configId: number;
}

interface CdcAssociation {
  firstId: number;
  count: number;
  clazz: number;
  subClass: number;
  protocol: number;
}

function descriptorError(message: string): UsbSerialDriverError {
  return new UsbSerialDriverError(401, `Invalid USB CDC descriptors: ${message}`);
}

function descriptorLength(raw: Uint8Array, offset: number, end: number): number {
  if (offset + 2 > end) {
    throw descriptorError('truncated descriptor header');
  }
  const length: number = raw[offset];
  if (length < 2 || offset + length > end) {
    throw descriptorError('descriptor length exceeds its boundary');
  }
  return length;
}

function parseConfiguration(raw: Uint8Array, start: number, end: number,
  configId: number): CdcInterfacePair[] {
  const kinds: Map<number, number> = new Map<number, number>();
  const relations: Map<number, number> = new Map<number, number>();
  const associations: CdcAssociation[] = [];
  let currentId: number = -1;
  let currentKind: number = -1;

  for (let offset: number = start; offset < end;) {
    const length: number = descriptorLength(raw, offset, end);
    const type: number = raw[offset + 1];
    if (type === 4) {
      if (length < 9) {
        throw descriptorError('truncated interface descriptor');
      }
      currentId = raw[offset + 2];
      currentKind = raw[offset + 5] === 2 && raw[offset + 6] === 2 ? 1 :
        (raw[offset + 5] === 10 ? 2 : 0);
      const previousKind: number | undefined = kinds.get(currentId);
      if (previousKind !== undefined && previousKind !== currentKind) {
        throw descriptorError('alternate settings disagree about interface class');
      }
      kinds.set(currentId, currentKind);
    } else if (type === 0x24) {
      if (length < 3) {
        throw descriptorError('truncated class interface descriptor');
      }
      if (raw[offset + 2] === 6) {
        if (length < 5 || currentId < 0) {
          throw descriptorError('truncated or unscoped Union descriptor');
        }
        if (currentKind === 2) {
          throw descriptorError('Union descriptor belongs to a data interface');
        }
        if (currentKind === 1) {
          if (length !== 5 || raw[offset + 3] !== currentId) {
            throw descriptorError('Union must name its ACM master and one slave');
          }
          const slaveId: number = raw[offset + 4];
          const previousSlave: number | undefined = relations.get(currentId);
          if (previousSlave !== undefined && previousSlave !== slaveId) {
            throw descriptorError('conflicting Union descriptors');
          }
          relations.set(currentId, slaveId);
        }
      }
    } else if (type === 0x0b) {
      if (length < 8) {
        throw descriptorError('truncated IAD descriptor');
      }
      const association: CdcAssociation = {
        firstId: raw[offset + 2], count: raw[offset + 3],
        clazz: raw[offset + 4], subClass: raw[offset + 5], protocol: raw[offset + 6]
      };
      if (association.count === 0 || association.firstId + association.count > 256) {
        throw descriptorError('invalid IAD interface range');
      }
      let duplicate: boolean = false;
      associations.forEach((previous: CdcAssociation) => {
        if (association.firstId < previous.firstId + previous.count &&
          previous.firstId < association.firstId + association.count) {
          if (association.firstId === previous.firstId && association.count === previous.count &&
            association.clazz === previous.clazz && association.subClass === previous.subClass &&
            association.protocol === previous.protocol) {
            duplicate = true;
          } else {
            throw descriptorError('overlapping IAD interface ranges');
          }
        }
      });
      if (!duplicate) {
        associations.push(association);
      }
    }
    offset += length;
  }

  associations.forEach((association: CdcAssociation) => {
    if (association.clazz !== 2 || association.subClass !== 2) {
      return;
    }
    const controls: number[] = [];
    const data: number[] = [];
    for (let id: number = association.firstId; id < association.firstId + association.count; ++id) {
      const kind: number | undefined = kinds.get(id);
      if (kind === undefined) {
        throw descriptorError('IAD refers to an absent interface');
      }
      if (kind === 1) { controls.push(id); }
      if (kind === 2) { data.push(id); }
    }
    if (controls.length !== 1 || data.length !== 1) {
      throw descriptorError('IAD must contain one ACM control and one data interface');
    }
    const previousSlave: number | undefined = relations.get(controls[0]);
    if (previousSlave !== undefined && previousSlave !== data[0]) {
      throw descriptorError('Union and IAD disagree');
    }
    relations.set(controls[0], data[0]);
  });

  const owners: Map<number, number> = new Map<number, number>();
  const pairs: CdcInterfacePair[] = [];
  relations.forEach((dataId: number, controlId: number) => {
    if (kinds.get(dataId) !== 2) {
      throw descriptorError('Union slave is not a CDC data interface');
    }
    associations.forEach((association: CdcAssociation) => {
      const controlInside: boolean = controlId >= association.firstId &&
        controlId < association.firstId + association.count;
      const dataInside: boolean = dataId >= association.firstId &&
        dataId < association.firstId + association.count;
      if (controlInside !== dataInside ||
        (controlInside && (association.clazz !== 2 || association.subClass !== 2))) {
        throw descriptorError('CDC pair conflicts with an IAD function');
      }
    });
    if (owners.has(dataId)) {
      throw descriptorError('data interface belongs to multiple ACM controls');
    }
    owners.set(dataId, controlId);
    pairs.push({ controlId: controlId, dataId: dataId, configId: configId });
  });
  return pairs;
}

export function parseCdcPairs(raw: Uint8Array, configId: number): CdcInterfacePair[] {
  if (configId < 1 || configId > 255 || Math.floor(configId) !== configId) {
    throw descriptorError('invalid configuration value');
  }
  const configurations: Set<number> = new Set<number>();
  let selected: boolean = false;
  let pairs: CdcInterfacePair[] = [];
  for (let offset: number = 0; offset < raw.length;) {
    const length: number = descriptorLength(raw, offset, raw.length);
    const type: number = raw[offset + 1];
    if (type !== 2) {
      if (type === 4 || type === 5 || type === 0x0b || type === 0x24) {
        throw descriptorError('interface descriptor is outside a configuration');
      }
      offset += length;
      continue;
    }
    if (length < 9) {
      throw descriptorError('truncated configuration header');
    }
    const total: number = raw[offset + 2] | (raw[offset + 3] << 8);
    const end: number = offset + total;
    if (total < length || end > raw.length) {
      throw descriptorError('configuration total length exceeds its boundary');
    }
    const value: number = raw[offset + 5];
    if (configurations.has(value)) {
      throw descriptorError('duplicate configuration value');
    }
    configurations.add(value);
    for (let child: number = offset + length; child < end;) {
      const childLength: number = descriptorLength(raw, child, end);
      if (raw[child + 1] === 2) {
        throw descriptorError('nested configuration descriptor');
      }
      child += childLength;
    }
    if (value === configId) {
      selected = true;
      pairs = parseConfiguration(raw, offset + length, end, configId);
    }
    offset = end;
  }
  if (!selected) {
    throw descriptorError('selected configuration is absent');
  }
  return pairs;
}
