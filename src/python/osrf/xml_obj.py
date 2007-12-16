import xml.dom.minidom

def xml_file_to_object(filename):
    """Turns the contents of an XML file into a Python object"""
    doc = xml.dom.minidom.parse(filename)
    obj = xml_node_to_object(doc.documentElement)
    doc.unlink()
    return obj

def xml_string_to_object(string):
    """Turns an XML string into a Python object"""
    doc = xml.dom.minidom.parseString(string)
    obj = xml_node_to_object(doc.documentElement)
    doc.unlink()
    return obj

def xml_node_to_object(xml_node):
    """Turns an XML node into a Python object"""
    obj = {}

    if xml_node.nodeType != xml_node.ELEMENT_NODE:
        return obj

    done = False
    node_name = xml_node.nodeName

    for node_child in xml_node.childNodes:
        if node_child.nodeType == xml_node.ELEMENT_NODE:
            sub_obj = xml_node_to_object(node_child)
            __append_child_node(obj, node_name, node_child.nodeName, sub_obj)
            done = True

    for attr in xml_node.attributes.values():
        __append_child_node(obj, node_name, attr.name,
            dict([(attr.name, attr.value)]))
        

    if not done and len(xml_node.childNodes) > 0:
        # If the node has no element children, clean up the text 
        # content and use that as the data
        text_node = xml_node.childNodes[0] # extract the text node
        data = unicode(text_node.nodeValue).replace('^\s*','')
        data = data.replace('\s*$','')

        if node_name in obj:
            # the current element contains attributes and text
            obj[node_name]['#text'] = data
        else:
            # the current element contains text only
            obj[node_name] = data

    return obj


def __append_child_node(obj, node_name, child_name, sub_obj):
    """ If a node has element children, create a new sub-object 
        for this node, attach an array for each type of child
        and recursively collect the children data into the array(s) """

    if not obj.has_key(node_name):
        obj[node_name] = {}

    if not obj[node_name].has_key(child_name):
        # we've encountered 1 sub-node with node_child's name
        if child_name in sub_obj:
            obj[node_name][child_name] = sub_obj[child_name]
        else:
            obj[node_name][child_name] = None

    else:
        if isinstance(obj[node_name][child_name], list):
            # we already have multiple sub-nodes with node_child's name
            obj[node_name][child_name].append(sub_obj[child_name])

        else:
            # we already have 1 sub-node with node_child's name, make 
            # it a list and append the current node
            val = obj[node_name][child_name]
            obj[node_name][child_name] = [ val, sub_obj[child_name] ]

