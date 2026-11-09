export function SelectLinks(links, query, group) {
  const search = query.trim().toLocaleLowerCase('ru');
  return links.filter(link => (!group || link.group === group) &&
    `${link.label} ${link.description ?? ''}`.toLocaleLowerCase('ru').includes(search));
}
