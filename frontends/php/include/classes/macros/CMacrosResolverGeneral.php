<?php
/*
** Zabbix
** Copyright (C) 2001-2015 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/


class CMacrosResolverGeneral {

	const PATTERN_HOST = '{(HOSTNAME|HOST\.HOST|HOST\.NAME)}';
	const PATTERN_HOST_ID = '{(HOST\.ID)}';
	const PATTERN_HOST_FUNCTION = '{(HOSTNAME|HOST\.HOST|HOST\.NAME)([1-9]?)}';
	const PATTERN_HOST_FUNCTION2 = '{(HOST\.ID|HOST\.HOST|HOST\.NAME)([1-9]?)}';
	const PATTERN_HOST_INTERNAL = 'HOST\.HOST|HOSTNAME';
	const PATTERN_MACRO_PARAM = '[1-9]?';
	const PATTERN_INTERFACE = '{(IPADDRESS|HOST\.IP|HOST\.DNS|HOST\.CONN)}';
	const PATTERN_INTERFACE_FUNCTION = '{(IPADDRESS|HOST\.IP|HOST\.DNS|HOST\.CONN|HOST\.PORT)([1-9]?)}';
	const PATTERN_INTERFACE_FUNCTION2 = '{(HOST\.IP|HOST\.DNS|HOST\.CONN|HOST\.PORT)([1-9]?)}';
	const PATTERN_INTERFACE_FUNCTION_WITHOUT_PORT = '{(IPADDRESS|HOST\.IP|HOST\.DNS|HOST\.CONN)([1-9]?)}';
	const PATTERN_ITEM_FUNCTION = '{(ITEM\.LASTVALUE|ITEM\.VALUE)([1-9]?)}';
	const PATTERN_ITEM_NUMBER = '\$[1-9]';
	const PATTERN_ITEM_MACROS = '{(HOSTNAME|HOST\.HOST|HOST\.NAME|IPADDRESS|HOST\.IP|HOST\.DNS|HOST\.CONN)}';
	const PATTERN_TRIGGER = '{(TRIGGER\.ID)}';

	/**
	 * Interface priorities.
	 *
	 * @var array
	 */
	protected $interfacePriorities = [
		INTERFACE_TYPE_AGENT => 4,
		INTERFACE_TYPE_SNMP => 3,
		INTERFACE_TYPE_JMX => 2,
		INTERFACE_TYPE_IPMI => 1
	];

	/**
	 * Work config name.
	 *
	 * @var string
	 */
	protected $config = '';

	/**
	 * Get reference macros for trigger.
	 * If macro reference non existing value it expands to empty string.
	 *
	 * @param string $expression
	 * @param string $text
	 *
	 * @return array
	 */
	protected function getTriggerReference($expression, $text) {
		$result = [];

		// search for reference macros $1, $2, $3, ...
		preg_match_all('/\$([1-9])/', $text, $refNumbers);

		if (empty($refNumbers)) {
			return $result;
		}

		// replace functionids with string 'function' to make values search easier
		$expression = preg_replace('/\{[0-9]+\}/', 'function', $expression);

		// search for numeric values in expression
		preg_match_all('/'.ZBX_PREG_NUMBER.'/', $expression, $values);

		foreach ($refNumbers[1] as $i) {
			$result['$'.$i] = isset($values[0][$i - 1]) ? $values[0][$i - 1] : '';
		}

		return $result;
	}

	/**
	 * Find macros in text by pattern.
	 *
	 * @param string $pattern
	 * @param array  $texts
	 *
	 * @return array
	 */
	protected function findMacros($pattern, array $texts) {
		$result = [];

		foreach ($texts as $text) {
			preg_match_all('/'.$pattern.'/', $text, $matches);

			$result = array_merge($result, $matches[0]);
		}

		return array_unique($result);
	}

	/**
	 * Find user macros.
	 *
	 * @param array  $texts
	 *
	 * @return array
	 */
	protected function findUserMacros(array $texts) {
		$result = [];

		foreach ($texts as $text) {
			$macros = (new CUserMacroParser($text, false))->getMacros();

			foreach ($macros as $macro) {
				$result[$macro['macro']] = true;
			}
		}

		return array_keys($result);
	}

	/**
	 * Find macros with function position.
	 *
	 * @param string $pattern
	 * @param string $text
	 *
	 * @return array	where key is found macro and value is array with related function position
	 */
	protected function findFunctionMacros($pattern, $text) {
		$result = [];

		preg_match_all('/'.$pattern.'/', $text, $matches);

		foreach ($matches[1] as $num => $macro) {
			$fNum = empty($matches[2][$num]) ? 0 : $matches[2][$num];

			$result[$macro][$fNum] = $fNum;
		}

		return $result;
	}

	/**
	 * Find function ids in trigger expression.
	 *
	 * @param string $expression
	 *
	 * @return array	where key is function id position in expression and value is function id
	 */
	protected function findFunctions($expression) {
		preg_match_all('/\{([0-9]+)\}/', $expression, $matches);

		$functions = [];

		foreach ($matches[1] as $i => $functionid) {
			$functions[$i + 1] = $functionid;
		}

		// macro without number is same as 1. but we need to distinguish them, so it's treated as 0
		if (isset($functions[1])) {
			$functions[0] = $functions[1];
		}

		return $functions;
	}

	/**
	 * Add function macro name with corresponding value to replace to $macroValues array.
	 *
	 * @param array  $macroValues
	 * @param array  $fNums
	 * @param int    $triggerId
	 * @param string $macro
	 * @param string $replace
	 *
	 * @return array
	 */
	protected function getFunctionMacroValues(array $macroValues, array $fNums, $triggerId, $macro, $replace) {
		foreach ($fNums as $fNum) {
			$macroValues[$triggerId][$this->getFunctionMacroName($macro, $fNum)] = $replace;
		}

		return $macroValues;
	}

	/**
	 * Get {ITEM.LASTVALUE} macro.
	 *
	 * @param mixed $lastValue
	 * @param array $item
	 *
	 * @return string
	 */
	protected function getItemLastValueMacro($lastValue, array $item) {
		return ($lastValue === null) ? UNRESOLVED_MACRO_STRING : formatHistoryValue($lastValue, $item);
	}

	/**
	 * Get function macro name.
	 *
	 * @param string $macro
	 * @param int    $fNum
	 *
	 * @return string
	 */
	protected function getFunctionMacroName($macro, $fNum) {
		return '{'.(($fNum == 0) ? $macro : $macro.$fNum).'}';
	}

	/**
	 * Get {ITEM.VALUE} macro.
	 * For triggers macro is resolved in same way as {ITEM.LASTVALUE} macro. Separate methods are created for event description,
	 * where {ITEM.VALUE} macro resolves in different way.
	 *
	 * @param mixed $lastValue
	 * @param array $item
	 * @param array $trigger
	 *
	 * @return string
	 */
	protected function getItemValueMacro($lastValue, array $item, array $trigger) {
		if ($this->config === 'eventDescription') {
			$value = item_get_history($item, $trigger['clock'], $trigger['ns']);

			return ($value === null) ? UNRESOLVED_MACRO_STRING : formatHistoryValue($value, $item);
		}
		else {
			return $this->getItemLastValueMacro($lastValue, $item);
		}
	}

	/**
	 * Get interface macros.
	 *
	 * @param array $macros
	 * @param array $macroValues
	 * @param bool  $port
	 *
	 * @return array
	 */
	protected function getIpMacros(array $macros, array $macroValues, $port) {
		if ($macros) {
			$selectPort = $port ? ',n.port' : '';

			$dbInterfaces = DBselect(
				'SELECT f.triggerid,f.functionid,n.ip,n.dns,n.type,n.useip'.$selectPort.
				' FROM functions f'.
					' JOIN items i ON f.itemid=i.itemid'.
					' JOIN interface n ON i.hostid=n.hostid'.
				' WHERE '.dbConditionInt('f.functionid', array_keys($macros)).
					' AND n.main=1'
			);

			// macro should be resolved to interface with highest priority ($priorities)
			$interfaces = [];

			while ($dbInterface = DBfetch($dbInterfaces)) {
				if (isset($interfaces[$dbInterface['functionid']])
						&& $this->interfacePriorities[$interfaces[$dbInterface['functionid']]['type']] > $this->interfacePriorities[$dbInterface['type']]) {
					continue;
				}

				$interfaces[$dbInterface['functionid']] = $dbInterface;
			}

			foreach ($interfaces as $interface) {
				foreach ($macros[$interface['functionid']] as $macro => $fNums) {
					switch ($macro) {
						case 'IPADDRESS':
						case 'HOST.IP':
							$replace = $interface['ip'];
							break;
						case 'HOST.DNS':
							$replace = $interface['dns'];
							break;
						case 'HOST.CONN':
							$replace = $interface['useip'] ? $interface['ip'] : $interface['dns'];
							break;
						case 'HOST.PORT':
							$replace = $interface['port'];
							break;
					}

					$macroValues = $this->getFunctionMacroValues($macroValues, $fNums, $interface['triggerid'], $macro, $replace);
				}
			}
		}

		return $macroValues;
	}

	/**
	 * Get item macros.
	 *
	 * @param array $macros
	 * @param array $triggers
	 * @param array $macroValues
	 *
	 * @return array
	 */
	protected function getItemMacros(array $macros, array $triggers, array $macroValues) {
		if ($macros) {
			$functions = DbFetchArray(DBselect(
				'SELECT f.triggerid,f.functionid,i.itemid,i.value_type,i.units,i.valuemapid'.
				' FROM functions f'.
					' JOIN items i ON f.itemid=i.itemid'.
					' JOIN hosts h ON i.hostid=h.hostid'.
				' WHERE '.dbConditionInt('f.functionid', array_keys($macros))
			));

			$history = Manager::History()->getLast($functions, 1, ZBX_HISTORY_PERIOD);

			// false passed to DBfetch to get data without null converted to 0, which is done by default
			foreach ($functions as $func) {
				foreach ($macros[$func['functionid']] as $macro => $fNums) {
					$lastValue = isset($history[$func['itemid']]) ? $history[$func['itemid']][0]['value'] : null;

					switch ($macro) {
						case 'ITEM.LASTVALUE':
							$replace = $this->getItemLastValueMacro($lastValue, $func);
							break;
						case 'ITEM.VALUE':
							$replace = $this->getItemValueMacro($lastValue, $func, $triggers[$func['triggerid']]);
							break;
					}

					$macroValues = $this->getFunctionMacroValues($macroValues, $fNums, $func['triggerid'], $macro, $replace);
				}
			}
		}

		return $macroValues;
	}

	/**
	 * Get host macros.
	 *
	 * @param array $macros
	 * @param array $macroValues
	 *
	 * @return array
	 */
	protected function getHostMacros(array $macros, array $macroValues) {
		if ($macros) {
			$dbFuncs = DBselect(
				'SELECT f.triggerid,f.functionid,h.hostid,h.host,h.name'.
				' FROM functions f'.
					' JOIN items i ON f.itemid=i.itemid'.
					' JOIN hosts h ON i.hostid=h.hostid'.
				' WHERE '.dbConditionInt('f.functionid', array_keys($macros))
			);
			while ($func = DBfetch($dbFuncs)) {
				foreach ($macros[$func['functionid']] as $macro => $fNums) {
					switch ($macro) {
						case 'HOST.ID':
							$replace = $func['hostid'];
							break;

						case 'HOSTNAME':
						case 'HOST.HOST':
							$replace = $func['host'];
							break;

						case 'HOST.NAME':
							$replace = $func['name'];
							break;
					}

					$macroValues = $this->getFunctionMacroValues($macroValues, $fNums, $func['triggerid'], $macro, $replace);
				}
			}
		}

		return $macroValues;
	}

	/**
	 * Is type available.
	 *
	 * @param string $type
	 *
	 * @return bool
	 */
	protected function isTypeAvailable($type) {
		return in_array($type, $this->configs[$this->config]['types']);
	}

	/**
	 * Get source field.
	 *
	 * @return string
	 */
	protected function getSource() {
		return $this->configs[$this->config]['source'];
	}

	/**
	 * Get macros with values.
	 *
	 * @param array $data			Macros to resolve ([hostids => [hostid], macros => [macro => null]])
	 *
	 * @return array
	 */
	protected function getUserMacros(array $data) {
		/*
		 * User macros
		 */
		$hostIds = [];

		foreach ($data as $element) {
			foreach ($element['hostids'] as $hostId) {
				$hostIds[$hostId] = $hostId;
			}
		}

		if (!$hostIds) {
			return $data;
		}

		// hostid => [templateid]
		$hostTemplates = [];

		// hostid => [macro => value]
		$hostMacros = [];

		do {
			$db_hosts = API::Host()->get([
				'output' => ['hostid'],
				'selectParentTemplates' => ['templateid'],
				'selectMacros' => ['macro', 'value'],
				'hostids' => $hostIds,
				'templated_hosts' => true
			]);

			$hostIds = [];

			if ($db_hosts) {
				foreach ($db_hosts as $db_host) {
					$hostTemplates[$db_host['hostid']] = zbx_objectValues($db_host['parentTemplates'], 'templateid');

					foreach ($db_host['macros'] as $db_macro) {
						if (!array_key_exists($db_host['hostid'], $hostMacros)) {
							$hostMacros[$db_host['hostid']] = [];
						}

						$hostMacros[$db_host['hostid']][$db_macro['macro']] = $db_macro['value'];
					}
				}

				foreach ($db_hosts as $db_host) {
					// Only unprocessed templates will be populated.
					foreach ($hostTemplates[$db_host['hostid']] as $templateId) {
						if (!array_key_exists($templateId, $hostTemplates)) {
							$hostIds[$templateId] = $templateId;
						}
					}
				}
			}
		} while ($hostIds);

		$allMacrosResolved = true;

		foreach ($data as &$element) {
			$hostIds = [];

			foreach ($element['hostids'] as $hostId) {
				$hostIds[$hostId] = $hostId;
			}

			natsort($hostIds);

			foreach ($element['macros'] as $macro => &$value) {
				$value = $this->getHostUserMacros($hostIds, $macro, $hostTemplates, $hostMacros);

				if ($value === null) {
					$allMacrosResolved = false;
				}
			}
			unset($value);
		}
		unset($element);

		if ($allMacrosResolved) {
			// there are no more hosts with unresolved macros
			return $data;
		}

		$macro_names = [];

		foreach ($data as $element) {
			foreach ($element['macros'] as $macro => $value) {
				$parsed_macro = (new CUserMacroParser($macro))->getMacros()[0];

				$macro_name = $parsed_macro['macro_name'];
				$context = $parsed_macro['context'];

				if ($context === null) {
					$macro_names['{$'.$macro_name] = true;
				}
				else {
					// Narrow down the search for macros with contexts.
					$macro_names['{$'.$macro_name.':'] = true;
				}
			}
		}

		// Find values in global macros.
		$db_globalmacros = API::UserMacro()->get([
			'output' => ['macro', 'value'],
			'search' => ['macro' => array_keys($macro_names)],
			'searchByAny' => true,
			'globalmacro' => true
		]);

		if ($db_globalmacros) {
			$allMacrosResolved = true;

			foreach ($data as &$element) {
				foreach ($element['macros'] as $macro => &$value) {
					$parsed_macro = (new CUserMacroParser($macro))->getMacros()[0];

					$macro_name = $parsed_macro['macro_name'];
					$context = $parsed_macro['context'];

					if ($value === null) {
						foreach ($db_globalmacros as $db_globalmacro) {
							$parsed_macro = (new CUserMacroParser($db_globalmacro['macro']))->getMacros()[0];

							$db_globalmacro_name = $parsed_macro['macro_name'];
							$db_globalmacro_context = $parsed_macro['context'];

							if ($macro_name === $db_globalmacro_name) {
								if ($db_globalmacro_context === null) {
									$value = $db_globalmacro['value'];
								}

								if ($context === $db_globalmacro_context) {
									$value = $db_globalmacro['value'];
									break;
								}
							}
						}

						if ($value === null) {
							$allMacrosResolved = false;
						}
					}
				}
				unset($value);
			}
			unset($element);

			if ($allMacrosResolved) {
				// there are no more hosts with unresolved macros
				return $data;
			}
		}

		/*
		 * Unresolved macros stay as is
		 */
		foreach ($data as &$element) {
			foreach ($element['macros'] as $macro => &$value) {
				if ($value === null) {
					$value = $macro;
				}
			}
			unset($value);
		}
		unset($element);

		return $data;
	}

	/**
	 * Get user macro from the requested hosts.
	 *
	 * @param array  $hostIds		The sorted list of hosts where macros will be looked for (hostid => hostid)
	 * @param string $macro			Macro to resolve
	 * @param array  $hostTemplates	The list of linked templates (hostid => [templateid])
	 * @param array  $hostMacros	The list of macros on hosts (hostid => [macro => value])
	 *
	 * @return array
	 */
	protected function getHostUserMacros(array $hostIds, $macro, array $hostTemplates, array $hostMacros) {
		$parsed_macro = (new CUserMacroParser($macro))->getMacros()[0];

		$macro_name = $parsed_macro['macro_name'];
		$context = $parsed_macro['context'];

		$value = null;

		foreach ($hostIds as $hostId) {
			if (array_key_exists($hostId, $hostMacros)) {
				foreach ($hostMacros[$hostId] as $hostmacro => $hostmacro_value) {
					$parsed_macro = (new CUserMacroParser($hostmacro))->getMacros()[0];

					$hostmacro_name = $parsed_macro['macro_name'];
					$hostmacro_context = $parsed_macro['context'];

					if ($macro_name === $hostmacro_name) {
						if ($hostmacro_context === null) {
							$value = $hostmacro_value;
						}

						if ($context === $hostmacro_context) {
							$value = $hostmacro_value;
							break;
						}
					}
				}

				return $value;
			}
		}

		if (!$hostTemplates) {
			return null;
		}

		$templateIds = [];

		foreach ($hostIds as $hostId) {
			if (isset($hostTemplates[$hostId])) {
				foreach ($hostTemplates[$hostId] as $templateId) {
					$templateIds[$templateId] = $templateId;
				}
			}
		}

		if ($templateIds) {
			natsort($templateIds);

			return $this->getHostUserMacros($templateIds, $macro, $hostTemplates, $hostMacros);
		}

		return null;
	}
}
